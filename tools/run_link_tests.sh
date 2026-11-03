#!/bin/sh
# Linker tests (build/رابط vs GNU ld).
#  1. tests/link/NAME/*.تجميع: assemble each file with مجمع, link them (in
#     sorted order) with رابط, link the same objects with GNU ld at the same
#     addresses, compare the loaded images; unless NAME.norun, run under qemu
#     and check NAME.code / NAME.out.
#  2. Every encoding-only test tests/asm/X.تجميع (X.norun) is linked with the
#     stub tests/link/عناوين_خارجية and compared with GNU ld the same way.
#  3. tests/link-errors/NAME/*.تجميع must fail to link with NAME.err in stderr.
#  4. A file that is not an ELF object must be rejected.
set -u
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AS="$ROOT/build/مجمع"
LD="$ROOT/build/رابط"
OUT="$ROOT/build/link-tests"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
mkdir -p "$OUT"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

# link_and_compare NAME OBJ... : our link + GNU link + image comparison
link_and_compare() {
    name=$1; shift
    "$LD" "$OUT/$name" "$@" &&
    python3 -I "$ROOT/tools/execcmp.py" script "$OUT/$name" "$OUT/$name.ld" &&
    "${PREFIX}ld" -e _بداية -T "$OUT/$name.ld" "$@" -o "$OUT/$name.gnu" &&
    python3 -I "$ROOT/tools/execcmp.py" compare "$OUT/$name" "$OUT/$name.gnu"
}

assemble_dir() { # dir prefix -> prints object paths
    for src in "$1"/*.تجميع; do
        obj="$OUT/$2.$(basename "$src" .تجميع).o"
        "$AS" "$src" "$obj" || return 1
        printf '%s\n' "$obj"
    done
}

for dir in "$ROOT"/tests/link/*/; do
    dir=${dir%/}
    name=$(basename "$dir")
    [ "$name" = عناوين_خارجية ] && continue
    ok=1
    objs=$(assemble_dir "$dir" "$name") || ok=0
    # shellcheck disable=SC2086
    [ $ok = 1 ] && { link_and_compare "$name" $objs || ok=0; }
    if [ $ok = 1 ] && [ ! -f "$ROOT/tests/link/$name.norun" ]; then
        qemu-riscv64 "$OUT/$name" > "$OUT/$name.stdout"
        code=$?
        want=0
        [ -f "$ROOT/tests/link/$name.code" ] && want=$(cat "$ROOT/tests/link/$name.code")
        [ "$code" = "$want" ] || { echo "  $name: exit code $code, expected $want"; ok=0; }
        if [ -f "$ROOT/tests/link/$name.out" ] && ! cmp -s "$ROOT/tests/link/$name.out" "$OUT/$name.stdout"; then
            echo "  $name: stdout differs"; ok=0
        fi
    fi
    report "link/$name" $ok
done

stub=$(assemble_dir "$ROOT/tests/link/عناوين_خارجية" stub)
for src in "$ROOT"/tests/asm/*.تجميع; do
    name=$(basename "$src" .تجميع)
    [ -f "$ROOT/tests/asm/$name.norun" ] || continue
    ok=1
    "$AS" "$src" "$OUT/cov.$name.o" && link_and_compare "cov.$name" "$OUT/cov.$name.o" $stub || ok=0
    report "link/coverage/$name" $ok
done

for dir in "$ROOT"/tests/link-errors/*/; do
    dir=${dir%/}
    name=$(basename "$dir")
    ok=1
    objs=$(assemble_dir "$dir" "err.$name") || { echo "  $name: assembly failed"; ok=0; }
    if [ $ok = 1 ]; then
        # shellcheck disable=SC2086
        DIAG_ORDER=file "$LD" "$OUT/err.$name" $objs 2> "$OUT/err.$name.stderr"
        code=$?
        [ "$code" = 1 ] || { echo "  link-errors/$name: exit code $code, expected 1"; ok=0; }
        grep -qF -- "$(cat "$ROOT/tests/link-errors/$name.err")" "$OUT/err.$name.stderr" ||
            { echo "  link-errors/$name: message not found:"; sed 's/^/    /' "$OUT/err.$name.stderr"; ok=0; }
    fi
    report "link-errors/$name" $ok
done

ok=1
DIAG_ORDER=file "$LD" "$OUT/bad" "$ROOT/README.md" 2> "$OUT/bad.stderr"
[ $? = 1 ] && grep -qF "ملف كائن غير صالح" "$OUT/bad.stderr" || ok=0
report "link-errors/not-an-object" $ok

echo "linker tests: $pass passed, $fail failed"
[ $fail = 0 ]
