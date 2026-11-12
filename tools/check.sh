#!/usr/bin/env bash
#
# tools/check.sh - the cJSON package gate.
#
#   bash tools/check.sh
#
# Static checks, then the behavioural gate:
#
#   1. the delivered file set exists and nothing under cJSON/docs/ does;
#   2. ct.pkg has the five expected keys, requires ct >= 0.20.3, and points at
#      third_party/ (never vendor/: `ct get` skips vendor/);
#   3. third_party/cJSON.{h,c} match tools/third_party.sha256 byte for byte;
#   4. no C type leaks into README examples / API tables / demo / tests
#      (the glossary section is exempt: it exists to spell out the
#      `pub type` mapping, so the C names there are the subject, not a leak),
#      and no free `pub fn` (every public method must have a type host);
#   5. the removed `toJSON()` / node-level `drop()` stay removed, the walker is
#      `nextSibling`, and the package never calls `cJSON_InitHooks(`;
#   6. README has exactly ten `## ` sections and its error table names every
#      `JsonStatus` code (both directions checked);
#   7. the 1k-vs-10k RSS bounds (tests/mem.sh, five modes);
#   8. an installed-layout build: a real `ct get` copies the package into
#      <tmp>/vendor/cJSON, `ct install` must print third_party flags and the
#      copied tree must still contain third_party/ (the installer skips
#      directories named `vendor/`); then build, run and diff the demo;
#   9. tests/run.sh (builds every suite with -Wall -Wextra, runs them, asserts
#      the compile-fail message and diffs the demo).
#
# Exit status: 0 only when every check passes.
#
# License: MIT (see LICENSE).

set -u

pkg="$(cd "$(dirname "$0")/.." && pwd)"
CT="${CT:-ct}"

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL  %s: %s\n' "$1" "$2"; }

work="${TMPDIR:-/tmp}/ct-cjson-check.$$"
cleanup() { rm -rf "$work"; }
trap cleanup EXIT HUP INT TERM
mkdir -p "$work" || exit 1

echo "ct: $("$CT" --version 2>&1)"
echo "cJSON gate: $pkg"
echo

echo "== delivered files =="
for f in ct.pkg LICENSE README.md \
         cJSON/Json.m.ct cJSON/JsonTypes.h.ct cJSON/Json.shim.h \
         third_party/cJSON.h third_party/cJSON.c third_party/LICENSE \
         tools/third_party.sha256 tools/check.sh \
         tests/run.sh tests/unit.ct tests/chain.ct tests/serialize.ct tests/sugar.ct \
         tests/mem.sh tests/mem.ct \
         tests/compile_fail/temp_receiver.ct \
         tests/compile_fail/enum_needs_types.ct \
         demo/main.ct demo/expected.out; do
    if [ -f "$pkg/$f" ]; then
        ok "file $f"
    else
        bad "file $f" "missing"
    fi
done
if [ -e "$pkg/docs" ]; then
    bad "docs/ absent" "the package documents through README.md only"
else
    ok "no cJSON/docs directory"
fi
echo

echo "== ct.pkg =="
for key in name version licence cflags ldflags requires; do
    if grep -q "^$key[[:space:]]*=" "$pkg/ct.pkg"; then
        ok "ct.pkg has $key"
    else
        bad "ct.pkg $key" "missing"
    fi
done
if grep -qx 'requires = ct >= 0.20.3' "$pkg/ct.pkg"; then
    ok "requires is ct >= 0.20.3"
else
    bad "requires" "expected 'ct >= 0.20.3'"
fi
if grep -q 'third_party/' "$pkg/ct.pkg"; then
    ok "cflags/ldflags point at third_party/"
else
    bad "third_party paths" "ct.pkg does not mention third_party/"
fi
if [ -d "$pkg/vendor" ]; then
    bad "no vendor/ directory" "ct get skips vendor/, so the dependency would vanish"
else
    ok "no package-local vendor/ directory"
fi
# C0.2 / C9.4: cJSON must not depend on std (the tests add std themselves).
if grep -Eq '^dep[[:space:]]*=[[:space:]]*std([[:space:]]|$)' "$pkg/ct.pkg"; then
    bad "C0.2/C9.4 dep = std" "cJSON must not pull std into consumers"
else
    ok "C0.2/C9.4 ct.pkg has no dep = std"
fi
echo

echo "== third_party checksums (cJSON 1.7.19, upstream v1.7.19) =="
if (cd "$pkg" && shasum -a 256 -c tools/third_party.sha256 >"$work/sha.log" 2>&1); then
    ok "cJSON.h and cJSON.c match tools/third_party.sha256"
else
    bad "checksums" "see log"
    cat "$work/sha.log" >&2
fi
if grep -q 'CJSON_VERSION_PATCH 19' "$pkg/third_party/cJSON.h"; then
    ok "cJSON.h is patch 19"
else
    bad "cJSON version" "CJSON_VERSION_PATCH is not 19"
fi
echo

echo "== public surface =="
if grep -q '^pub fn ' "$pkg/cJSON/Json.m.ct"; then
    bad "no free pub fn" "every method must hang off a type or namespace host"
else
    ok "no free pub fn in Json.m.ct"
fi
# C1.1: strip comments, the two `pub type` alias lines and everything right of
# `->` (the C recipe), then no `cJSON *` / `struct cJSON` / `cJSON <ident>` /
# `struct ct_json_doc` may remain. `cJSON` as the package namespace
# (`namespace cJSON;`, `map trait Lib for namespace cJSON`) is not a C type and
# must not count.
sig="$(awk '
    {
        line = $0
        if (incomment) {
            if (line ~ /\*\//) { sub(/^.*\*\//, "", line); incomment = 0 } else next
        }
        if (line ~ /\/\*/) {
            if (line ~ /\*\//) {
                sub(/\/\*.*\*\//, "", line)
            } else {
                sub(/\/\*.*/, "", line)
                incomment = 1
            }
        }
        sub(/\/\/.*/, "", line)
        if (line ~ /^[[:space:]]*pub[[:space:]]+type[[:space:]]/) next
        sub(/->.*/, "", line)
        print line
    }' "$pkg/cJSON/Json.m.ct")"
ci1_cjson="$(printf '%s\n' "$sig" | grep -Ec 'struct[[:space:]]+cJSON|cJSON[[:space:]]*\*|cJSON[[:space:]]+[A-Za-z_]')"
ci1_doc="$(printf '%s\n' "$sig" | grep -c 'struct ct_json_doc')"
if [ "$ci1_cjson" -eq 0 ] && [ "$ci1_doc" -eq 0 ]; then
    ok "C1.1 no cJSON / ct_json_doc in the public signatures"
else
    bad "C1.1 C type leak" "cJSON=$ci1_cjson ct_json_doc=$ci1_doc after stripping aliases and recipes"
fi
# C1.12: no generated global may collide with a global defined by
# third_party/cJSON.c. `--emit-c` stops before linking, so a collision is
# reported as a symbol-set fact instead of a cc/link error. Any generated
# symbol under the upstream `cJSON_` prefix is a failure even when the current
# vendored version happens not to define that name.
symdir="$work/sym"
mkdir -p "$symdir/emit" "$symdir/obj"
if ( cd "$symdir" && "$CT" build --emit-c --outdir "$symdir/emit" -I "$pkg" "$pkg/demo/main.ct" ) \
        >"$symdir/emit.log" 2>&1; then
    symflags="-std=c99 -I $pkg/third_party -DCJSON_NESTING_LIMIT=64 -I $pkg/cJSON -I $symdir/emit"
    symok=1
    for src in "$symdir"/emit/*.c; do
        "${CC:-cc}" $symflags -c "$src" -o "$symdir/obj/$(basename "$src" .c).o" \
            2>>"$symdir/emit.log" || symok=0
    done
    "${CC:-cc}" $symflags -c "$pkg/third_party/cJSON.c" -o "$symdir/obj/cJSON.o" \
        2>>"$symdir/emit.log" || symok=0
    if [ "$symok" -eq 1 ]; then
        nm -g --defined-only "$symdir"/obj/*.o 2>/dev/null \
            | awk 'NF==3 {print $3}' | sed 's/^_//' | sort -u > "$symdir/all.syms"
        nm -g --defined-only "$symdir/obj/cJSON.o" 2>/dev/null \
            | awk 'NF==3 {print $3}' | sed 's/^_//' | sort -u > "$symdir/upstream.syms"
        comm -23 "$symdir/all.syms" "$symdir/upstream.syms" > "$symdir/gen.syms"
        sym_inter="$(comm -12 "$symdir/gen.syms" "$symdir/upstream.syms" | tr '\n' ' ')"
        sym_prefix="$(grep -E '^cJSON_' "$symdir/gen.syms" | tr '\n' ' ')"
        if [ -z "$sym_inter" ] && [ -z "$sym_prefix" ]; then
            ok "C1.12 generated symbols are disjoint from third_party/cJSON.c"
        else
            bad "C1.12 symbol collision" "intersection=[$sym_inter] cJSON_prefix=[$sym_prefix]"
        fi
    else
        bad "C1.12 symbol collision" "generated C did not compile; see $symdir/emit.log"
    fi
else
    bad "C1.12 symbol collision" "ct build --emit-c failed; see $symdir/emit.log"
fi
# C1.1 (tightened): the strip above skips `pub type` lines because `JsonDoc`
# legitimately aliases the package-private `ct_json_doc` tag. That would also
# hide a regression of the node alias itself, so pin that one line: `JsonNode`
# must name the package handle `ct_json_node`, never the vendored `cJSON`.
if grep -Eq '^[[:space:]]*pub[[:space:]]+type[[:space:]]+JsonNode[[:space:]]*=[[:space:]]*struct[[:space:]]+ct_json_node[[:space:]]*;' \
        "$pkg/cJSON/Json.m.ct"; then
    ok "C1.1 JsonNode aliases the package handle, not the vendored struct"
else
    bad "C1.1 JsonNode alias" "expected 'pub type JsonNode = struct ct_json_node;'"
fi
# C1.9: the Lib host is exactly the seven constructors; a `set*` smuggled onto
# the namespace would change the configuration story.
lib_methods="$(awk '
    /^map trait Lib for namespace cJSON/ { inblock = 1; next }
    inblock && /^[[:space:]]*}/ { inblock = 0 }
    inblock {
        line = $0
        sub(/^[[:space:]]*/, "", line)
        if (line ~ /^[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/) {
            sub(/[[:space:]]*\(.*/, "", line)
            print line
        }
    }' "$pkg/cJSON/Json.m.ct" | sort | tr '\n' ' ')"
lib_expected="newArray newBool newDoc newNull newNumber newObject newString "
if [ "$lib_methods" = "$lib_expected" ]; then
    ok "C1.9 Lib host is exactly the 7 constructors"
else
    bad "C1.9 Lib host" "got [$lib_methods], want [$lib_expected]"
fi
for host in 'map trait Node for type JsonNode \*' 'map trait Doc for type JsonDoc \*' \
            'map trait Text for type char \*' 'map trait Lib for namespace cJSON'; do
    if grep -Eq "$host" "$pkg/cJSON/Json.m.ct"; then
        ok "host: $host"
    else
        bad "host: $host" "missing"
    fi
done
# C1.3 / C1.4: the declared return types, not only the runtime identity.
while IFS= read -r sig; do
    if grep -Fq "$sig" "$pkg/cJSON/Json.m.ct"; then
        ok "C1.3/C1.4 signature: $sig"
    else
        bad "C1.3/C1.4 signature: $sig" "not declared"
    fi
done < <(printf '%s\n' \
    'JsonDoc *setMaxDepth(' 'JsonDoc *setMaxNodes(' \
    'JsonDoc *setMaxString(' 'JsonDoc *setMaxInput(' \
    'JsonDoc *setRoot(' 'JsonNode *put(' 'JsonNode *append(' 'JsonDoc *newDoc(' \
    'JsonNode *newNull(' 'JsonNode *newBool(' 'JsonNode *newNumber(' \
    'JsonNode *newString(' 'JsonNode *newArray(' 'JsonNode *newObject(')
# C1.2: C type names must not appear on the copy-paste surface - the examples,
# API tables and prose of README, plus demo/ and tests/. The glossary is the
# single exempt section: its declared purpose is to spell out the identity
# mapping (`pub type JsonNode = struct ct_json_node` / `= struct ct_json_doc`),
# so there the C name is the subject being explained, not a token a reader would
# copy into their own code. The exemption is section-scoped (`## ...术语表`
# until the next `^## `); every other section is still scanned line by line,
# so a `cJSON *` injected into any example block still fails the gate.
leak_pat='cJSON ?\*|struct cJSON|ct_json_'
leaks="$(
    {
        awk '
            /^## / { in_glossary = ($0 ~ /术语表/ || $0 ~ /Glossary/) }
            !in_glossary { print FILENAME ":" FNR "|" $0 }
        ' "$pkg/README.md"
        grep -rEn "$leak_pat" "$pkg/demo" "$pkg/tests" 2>/dev/null || true
    } | grep -E "$leak_pat" || true
)"
if [ -z "$leaks" ]; then
    ok "no C types in README examples / API docs / demo / tests"
else
    bad "C type leak" "$(printf '%s' "$leaks" | head -3)"
fi
if grep -rq 'toJSON\|ct_json_text_to_json\|ct_json_node_drop' "$pkg/cJSON" "$pkg/tests" "$pkg/demo" 2>/dev/null; then
    bad "removed surface" "toJSON / node drop reappeared"
else
    ok "toJSON and node-level drop stay removed"
fi
if grep -q 'nextSibling' "$pkg/cJSON/Json.m.ct" && ! grep -q 'int next()' "$pkg/cJSON/Json.m.ct"; then
    ok "the walker is nextSibling only"
else
    bad "walker name" "expected nextSibling and no next"
fi
# C6.7: the package must never install its own allocator hooks; only the
# upstream definition may mention InitHooks, so scan the package code alone.
hooks="$(grep -rn 'cJSON_InitHooks(' "$pkg/cJSON" 2>/dev/null || true)"
if [ -z "$hooks" ]; then
    ok "C6.7 no cJSON_InitHooks( call in the package code"
else
    bad "C6.7 allocator hook" "$(printf '%s' "$hooks" | head -3)"
fi
# C0.5: JsonTypes carries enums only - no functions, traits or structs.
if grep -Eq '^(pub[[:space:]]+)?(fn|struct|trait|impl|use|const)[[:space:]]' \
        "$pkg/cJSON/JsonTypes.h.ct"; then
    bad "C0.5 JsonTypes" "only enums may live in JsonTypes.h.ct"
else
    ok "C0.5 JsonTypes holds enums only"
fi
# C0.6: every definition in the shim is `static inline` (no emitted symbols).
shim_bad="$(awk '
    {
        line = $0
        if (incomment) {
            if (line ~ /\*\//) { sub(/^.*\*\//, "", line); incomment = 0 } else next
        }
        if (line ~ /\/\*/) {
            if (line ~ /\*\//) {
                sub(/\/\*.*\*\//, "", line)
            } else {
                sub(/\/\*.*/, "", line)
                incomment = 1
            }
        }
        if (line ~ /^[[:space:]]*$/) next
        if (line ~ /^[[:space:]]/) next
        if (line ~ /^#/) next
        if (line ~ /^typedef /) next
        if (line ~ /^struct /) next
        if (line ~ /^\{/) next
        if (line ~ /^\}/) next
        if (line ~ /^static inline /) next
        print
    }' "$pkg/cJSON/Json.shim.h")"
if [ -z "$shim_bad" ]; then
    ok "C0.6 every shim definition is static inline"
else
    bad "C0.6 shim linkage" "$(printf '%s' "$shim_bad" | head -3)"
fi
# C7.5: positions come from the strict scanner, never from cJSON's pointer.
if grep -rq 'cJSON_GetErrorPtr' "$pkg/cJSON"; then
    bad "C7.5 GetErrorPtr" "the shim must not fall back to cJSON_GetErrorPtr"
else
    ok "C7.5 no cJSON_GetErrorPtr in the package code"
fi
echo

echo "== README =="
sections="$(grep -c '^## ' "$pkg/README.md" 2>/dev/null || true)"
if [ "$sections" = "10" ]; then
    ok "README has exactly 10 sections"
else
    bad "README sections" "found $sections, expected 10"
fi
echo

echo "== error codes (README vs JsonStatus) =="
status_names="$(awk '
    /^pub enum JsonStatus/ { inblock = 1; next }
    inblock && /^}/ { inblock = 0 }
    inblock {
        line = $0
        while (match(line, /JSON_[A-Z0-9_]+/)) {
            print substr(line, RSTART, RLENGTH)
            line = substr(line, RSTART + RLENGTH)
        }
    }' "$pkg/cJSON/JsonTypes.h.ct" | sort | tr '\n' ' ')"
status_expected="JSON_ERR_BOM JSON_ERR_CONTROL JSON_ERR_DUP_KEY JSON_ERR_LIMIT JSON_ERR_NONFINITE JSON_ERR_SYNTAX JSON_ERR_UTF8 JSON_OK "
if [ "$status_names" = "$status_expected" ]; then
    ok "JsonStatus declares exactly the 8 documented codes"
else
    bad "JsonStatus codes" "got [$status_names], want [$status_expected]"
fi
for code in $status_expected; do
    if grep -Fq "$code" "$pkg/README.md"; then
        ok "README documents $code"
    else
        bad "README $code" "missing from the error table"
    fi
done
echo

echo "== memory (1k vs 10k, two runs each) =="
if bash "$pkg/tests/mem.sh" >"$work/mem.log" 2>&1; then
    for mode in fail ok todoc write setroot; do
        for kind in differential absolute; do
            line="$(grep "  ok    $mode $kind" "$work/mem.log" | head -1)"
            if [ -n "$line" ]; then
                ok "${line#  ok    }"
            else
                bad "$mode $kind" "tests/mem.sh printed no decision line"
            fi
        done
    done
else
    bad "tests/mem.sh" "see log"
    cat "$work/mem.log" >&2
fi
echo

echo "== installed layout (real ct get + ct install) =="
if ! command -v "$CT" >/dev/null 2>&1; then
    bad "installed build" "ct not found: $CT"
else
    # A consumer manifest is required before `ct get` will record the dep.
    printf 'name = jsonconsumer\nversion = 0.1.0\nlicence = MIT\n' > "$work/ct.pkg"
    install_ok=1
    if ! (cd "$work" && "$CT" get "$pkg") >"$work/get.log" 2>&1; then
        bad "ct get" "see log"
        cat "$work/get.log" >&2
        install_ok=0
    elif [ ! -f "$work/vendor/cJSON/third_party/cJSON.h" ] ||
         [ ! -f "$work/vendor/cJSON/third_party/cJSON.c" ]; then
        # Regression guard: `ct get`'s copy_tree skips directories named
        # `vendor`, which is exactly how the old layout shipped broken.
        bad "ct get copy" "third_party/ did not survive the install copy"
        ls -R "$work/vendor/cJSON" >&2
        install_ok=0
    elif [ -d "$work/vendor/cJSON/vendor" ]; then
        bad "ct get copy" "the installed package still carries a vendor/ directory"
        install_ok=0
    else
        ok "ct get copies third_party/ into vendor/cJSON"
    fi
    if [ "$install_ok" -eq 1 ]; then
        if ! (cd "$work" && "$CT" install) >"$work/install.log" 2>&1; then
            bad "ct install" "see log"
            cat "$work/install.log" >&2
            install_ok=0
        elif grep -q 'vendor/cJSON/third_party' "$work/install.log"; then
            ok "ct install prints the third_party flags"
        else
            bad "ct install flags" "third_party missing from the printed command"
            cat "$work/install.log" >&2
            install_ok=0
        fi
    fi
    # C9.4: the installed command must not drag std in.
    if grep -q 'vendor/std' "$work/install.log" 2>/dev/null; then
        bad "C9.4 install flags" "the printed command references vendor/std"
        install_ok=0
    else
        ok "C9.4 install command has no vendor/std"
    fi
    if [ "$install_ok" -eq 1 ]; then
        (
            cd "$work" || exit 1
            "$CT" build -I vendor/cJSON \
                --cflags "-I vendor/cJSON/third_party -DCJSON_NESTING_LIMIT=64 -Wall -Wextra" \
                --ldflags "vendor/cJSON/third_party/cJSON.c -lm" \
                vendor/cJSON/demo/main.ct -o demo_installed
        ) >"$work/installed.log" 2>&1
        if [ $? -eq 0 ] && ! grep -q 'warning:' "$work/installed.log"; then
            if "$work/demo_installed" >"$work/installed.out" 2>&1 &&
               diff -u "$pkg/demo/expected.out" "$work/installed.out" >"$work/installed.diff" 2>&1; then
                ok "install-layout build, run and output"
            else
                bad "install-layout run" "see diff"
                cat "$work/installed.diff" >&2
            fi
        else
            bad "install-layout build" "see log"
            cat "$work/installed.log" >&2
        fi
    fi
fi
echo

echo "== generated C (C11.6) =="
mkdir -p "$work/emit"
if (cd "$pkg" && "$CT" build -I "$pkg" \
        --cflags "-I $pkg/third_party -DCJSON_NESTING_LIMIT=64" \
        --ldflags "$pkg/third_party/cJSON.c -lm" \
        --emit-c --outdir "$work/emit" demo/main.ct) >"$work/emit.log" 2>&1; then
    emitc="$(grep -l 'Json.shim.h' "$work/emit"/*.c 2>/dev/null | head -1)"
    if [ -n "$emitc" ]; then
        ok "C11.6 --emit-c output includes Json.shim.h"
    else
        bad "C11.6 emit-c header" "no generated .c mentions Json.shim.h"
        ls -l "$work/emit" >&2
    fi
else
    bad "C11.6 emit-c build" "see log"
    cat "$work/emit.log" >&2
fi
echo

echo "== suites =="
if bash "$pkg/tests/run.sh" >"$work/run.log" 2>&1; then
    ok "tests/run.sh ($(tail -n 1 "$work/run.log"))"
else
    bad "tests/run.sh" "see log"
    cat "$work/run.log" >&2
fi
echo

printf 'cJSON gate: %d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
