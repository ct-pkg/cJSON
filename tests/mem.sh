#!/usr/bin/env bash
#
# tests/mem.sh - executable assertions for C6.2 / C6.3 / C6.5, C4.10 and C4.11.
#
# Leak signal is the *delta* between two iteration counts, not an absolute
# ceiling: every mode runs n=1000 and n=10000 (twice each) and must satisfy
#
#     rss_kb(10000) - rss_kb(1000) <= MEM_DIFF_KB      (default 256)
#
# On macOS / arm64 a clean build measures -16..+16 KB across runs, while a
# 48 B/iter leak measures about +430 KB at 10k iterations - the threshold sits
# ~16x above the noise and well below the signal, so 48 B/iter and 64 B/iter
# leaks both go red.
#
# Modes: fail (rejected parse), ok (accepted parse), todoc (char * -> doc),
# write (parse + compact/pretty render, C4.10) and setroot (repeated setRoot,
# which must free the previous tree, C4.11).
#
# The absolute ceiling (MEM_ABS_KB, default 4096) is only a coarse net for
# whole-document leaks (a forgotten cJSON_Delete is ~1.4 KB/iter, i.e. ~14 MB
# at 10k); the differential is the primary judge.
#
#   bash tests/mem.sh
#   CT=/path/to/ct bash tests/mem.sh
#   MEM_DIFF_KB=128 MEM_ABS_KB=2048 bash tests/mem.sh   # tighter only
#
# Exit status: 0 only when every mode passes both judgments.
#
# License: MIT (see LICENSE).

set -u

here="$(cd "$(dirname "$0")" && pwd)"
pkg="$(cd "$here/.." && pwd)"
repo="$(cd "$pkg/.." && pwd)"
CT="${CT:-ct}"
diff_kb="${MEM_DIFF_KB:-256}"
abs_kb="${MEM_ABS_KB:-4096}"
small=1000
large=10000

# An environment override may only *tighten* the judge. Loosening it would let
# a leaking package pass, so any larger value is clamped back to the default.
if [ "$diff_kb" -gt 256 ]; then
    diff_kb=256
fi
if [ "$abs_kb" -gt 4096 ]; then
    abs_kb=4096
fi

if ! command -v "$CT" >/dev/null 2>&1; then
    echo "ct not found: $CT" >&2
    exit 1
fi

work="${TMPDIR:-/tmp}/ct-json-mem.$$"
rm -rf "$work"
mkdir -p "$work/roots" || exit 1

# `ct build` honours a single -I; mem.ct imports json:: and std::str::.
ln -s "$pkg/json" "$work/roots/json"
if [ -d "$repo/std/std" ]; then
    ln -s "$repo/std/std" "$work/roots/std"
fi

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL  %s: %s\n' "$1" "$2"; }

echo "ct: $(command -v "$CT") ($("$CT" --version 2>/dev/null || true))"
echo "judge: delta(10k-1k) <= ${diff_kb} KB; absolute 10k <= ${abs_kb} KB"
echo

if (
    cd "$work" || exit 1
    "$CT" build -I "$work/roots" \
        --cflags "-I $pkg/third_party -DCJSON_NESTING_LIMIT=64 -Wall -Wextra" \
        --ldflags "$pkg/third_party/cJSON.c -lm" \
        "$here/mem.ct" -o mem
) >"$work/build.log" 2>&1; then
    if grep -q 'warning:' "$work/build.log"; then
        bad "mem build" "$(grep -m1 'warning:' "$work/build.log")"
        exit 1
    fi
else
    bad "mem build" "see log"
    cat "$work/build.log" >&2
    exit 1
fi

# run_rss <mode> <n> <tag> -> prints rss in KB, returns non-zero on failure
run_rss() {
    local mode="$1"
    local n="$2"
    local tag="$3"
    local rc bytes
    /usr/bin/time -l "$work/mem" "$mode" "$n" >"$work/$tag.out" 2>"$work/$tag.time"
    rc=$?
    bytes="$(awk '/maximum resident set size/ { print $1; exit }' "$work/$tag.time")"
    if [ "$rc" -ne 0 ] || [ -z "$bytes" ]; then
        echo ""
        return 1
    fi
    echo $((bytes / 1024))
}

for mode in fail ok todoc write setroot; do
    a1="$(run_rss "$mode" "$small" "${mode}1a")" || a1=""
    a2="$(run_rss "$mode" "$small" "${mode}1b")" || a2=""
    b1="$(run_rss "$mode" "$large" "${mode}2a")" || b1=""
    b2="$(run_rss "$mode" "$large" "${mode}2b")" || b2=""
    if [ -z "$a1" ] || [ -z "$a2" ] || [ -z "$b1" ] || [ -z "$b2" ]; then
        bad "$mode loop" "one of the four runs failed; see $work"
        continue
    fi
    # max over the two runs at each size keeps the judgment conservative.
    rss1=$((a1 > a2 ? a1 : a2))
    rss2=$((b1 > b2 ? b1 : b2))
    printf '  run   %-5s 1k: %s %s KB | 10k: %s %s KB\n' "$mode" "$a1" "$a2" "$b1" "$b2"
    delta=$((rss2 - rss1))
    if [ "$delta" -le "$diff_kb" ]; then
        ok "$mode differential 1k=$rss1 10k=$rss2 delta=${delta} KB <= ${diff_kb} KB"
    else
        bad "$mode differential" "1k=$rss1 10k=$rss2 delta=${delta} KB > ${diff_kb} KB"
    fi
    if [ "$rss2" -le "$abs_kb" ]; then
        ok "$mode absolute 10k=$rss2 KB <= ${abs_kb} KB"
    else
        bad "$mode absolute" "10k=$rss2 KB > ${abs_kb} KB"
    fi
done

echo
printf 'mem: %d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
