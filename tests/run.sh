#!/usr/bin/env bash
#
# tests/run.sh - build and run the json package suites.
#
#   1. builds unit / chain / serialize / sugar with -Wall -Wextra and fails on
#      any compiler warning;
#   2. runs each and requires `SUMMARY passed=N failed=0` (and exit 0);
#   3. builds tests/compile_fail/temp_receiver.ct, which MUST fail with the
#      recorded "method call on a temporary" message;
#   4. builds demo/main.ct, runs it and diffs the output with
#      demo/expected.out.
#
#   bash tests/run.sh
#   CT=/path/to/ct bash tests/run.sh
#
# ct v0.20.3 or newer is required (pub type aliases). All temporary files live
# under $TMPDIR and are removed on exit.
#
# License: MIT (see LICENSE).

set -u

here="$(cd "$(dirname "$0")" && pwd)"
pkg="$(cd "$here/.." && pwd)"
repo="$(cd "$pkg/.." && pwd)"
CT="${CT:-ct}"

if ! command -v "$CT" >/dev/null 2>&1; then
    echo "ct not found: $CT" >&2
    echo "set CT=/path/to/ct (ct >= 0.20.3, with type aliases)" >&2
    exit 1
fi

work="${TMPDIR:-/tmp}/ct-json-test.$$"
rm -rf "$work"
mkdir -p "$work/roots" || exit 1

# `ct build` honours a single -I, but the suites import both `json::` and
# `std::str::`. One root with two symlinks covers both.
ln -s "$pkg/json" "$work/roots/json"
if [ -d "$repo/std/std" ]; then
    ln -s "$repo/std/std" "$work/roots/std"
fi

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

cflags="-I $pkg/third_party -DCJSON_NESTING_LIMIT=64 -Wall -Wextra"
ldflags="$pkg/third_party/cJSON.c -lm"

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL  %s: %s\n' "$1" "$2"; }

echo "ct: $(command -v "$CT") ($("$CT" --version 2>/dev/null || true))"
echo "pkg: $pkg"
echo

build() { # build <source> <output> <log>
    (
        cd "$work" || exit 1
        "$CT" build -I "$work/roots" --cflags "$cflags" --ldflags "$ldflags" \
            "$1" -o "$2"
    ) >"$3" 2>&1
}

for suite in unit chain serialize sugar; do
    echo "== $suite =="
    if ! build "$here/$suite.ct" "$work/$suite" "$work/$suite.build.log"; then
        bad "$suite build" "see log"
        cat "$work/$suite.build.log" >&2
        continue
    fi
    if grep -q 'warning:' "$work/$suite.build.log"; then
        bad "$suite warning" "$(grep -m1 'warning:' "$work/$suite.build.log")"
    else
        ok "$suite build (zero warnings)"
    fi

    "$work/$suite" >"$work/$suite.out" 2>"$work/$suite.err"
    run_rc=$?
    if [ -s "$work/$suite.err" ]; then
        cat "$work/$suite.err" >&2
    fi

    summary="$(sed -n 's/^SUMMARY passed=\([0-9][0-9]*\) failed=\([0-9][0-9]*\)$/\1 \2/p' "$work/$suite.out")"
    if [ -z "$summary" ]; then
        bad "$suite run" "no SUMMARY line (exit $run_rc)"
        cat "$work/$suite.out" >&2
    else
        s_pass="${summary% *}"
        s_fail="${summary#* }"
        if [ "$run_rc" -eq 0 ] && [ "$s_fail" -eq 0 ]; then
            ok "$suite assertions ($s_pass passed)"
        else
            bad "$suite assertions" "exit $run_rc, $s_fail failed"
            grep '^FAIL ' "$work/$suite.out" || true
        fi
    fi
    echo
done

echo "== compile_fail =="
if build "$here/compile_fail/temp_receiver.ct" "$work/temp_receiver" "$work/cf.log"; then
    bad "temp_receiver" "compiled, but the temporary rule should reject it"
else
    if grep -q 'a method call on a temporary is only allowed at the start of a statement or of an assignment' "$work/cf.log"; then
        ok "temp_receiver rejected with the recorded message"
    else
        bad "temp_receiver" "failed with a different message; see log"
        cat "$work/cf.log" >&2
    fi
fi
if build "$here/compile_fail/enum_needs_types.ct" "$work/enum_needs_types" "$work/cf_enum.log"; then
    bad "enum_needs_types" "compiled, but JSON_OK should need json::JsonTypes"
else
    if grep -q 'JSON_OK' "$work/cf_enum.log"; then
        ok "C9.3 JSON_OK needs json::JsonTypes"
    else
        bad "enum_needs_types" "failed with a different message; see log"
        cat "$work/cf_enum.log" >&2
    fi
fi
echo

echo "== demo =="
if ! build "$pkg/demo/main.ct" "$work/demo" "$work/demo.build.log"; then
    bad "demo build" "see log"
    cat "$work/demo.build.log" >&2
else
    if grep -q 'warning:' "$work/demo.build.log"; then
        bad "demo warning" "$(grep -m1 'warning:' "$work/demo.build.log")"
    else
        ok "demo build (zero warnings)"
    fi
    "$work/demo" >"$work/demo.out" 2>&1
    if diff -u "$pkg/demo/expected.out" "$work/demo.out" >"$work/demo.diff" 2>&1; then
        ok "demo output matches expected.out"
    else
        bad "demo output" "diff vs expected.out"
        cat "$work/demo.diff" >&2
    fi
fi
echo

printf 'json: %d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
