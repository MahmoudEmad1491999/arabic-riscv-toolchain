#!/bin/sh
# Assembler tests.
#  1. tests/asm/NAME.تجميع: assemble with ours (build/مجمع) and with GNU as
#     (via tools/translit.py, -mno-relax); compare with tools/elfcmp.py.
#     Unless NAME.norun exists, link OUR object with GNU ld, run it under
#     qemu-riscv64 and check NAME.code (exit code, default 0) / NAME.out.
#  2. tests/errors/NAME.تجميع: ours must fail (exit 1) and stderr (in file
#     order, DIAG_ORDER=file) must contain the text in NAME.err.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AS="$ROOT/build/مجمع"
OUT="$ROOT/build/asm-tests"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
mkdir -p "$OUT"
pass=0; fail=0

report() { # name ok
    if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi
}

for src in "$ROOT"/tests/asm/*.تجميع; do
    name=$(basename "$src" .تجميع)
    dir=$(dirname "$src")
    ok=1
    python3 -I "$ROOT/tools/translit.py" "$src" "$OUT/$name.ref.s" &&
    "${PREFIX}as" -march=rv64i -mabi=lp64 -mno-relax "$OUT/$name.ref.s" -o "$OUT/$name.ref.o" || { echo "  $name: reference assembly failed"; ok=0; }
    "$AS" "$src" "$OUT/$name.o" || { echo "  $name: مجمع failed"; ok=0; }
    if [ $ok = 1 ]; then
        python3 -I "$ROOT/tools/elfcmp.py" "$OUT/$name.o" "$OUT/$name.ref.o" || ok=0
    fi
    if [ $ok = 1 ] && [ ! -f "$dir/$name.norun" ]; then
        if "${PREFIX}ld" -e _بداية "$OUT/$name.o" -o "$OUT/$name"; then
            qemu-riscv64 "$OUT/$name" > "$OUT/$name.stdout"
            code=$?
            want=0
            [ -f "$dir/$name.code" ] && want=$(cat "$dir/$name.code")
            [ "$code" = "$want" ] || { echo "  $name: exit code $code, expected $want"; ok=0; }
            if [ -f "$dir/$name.out" ] && ! cmp -s "$dir/$name.out" "$OUT/$name.stdout"; then
                echo "  $name: stdout differs"; ok=0
            fi
        else
            echo "  $name: link failed"; ok=0
        fi
    fi
    report "asm/$name" $ok
done

for src in "$ROOT"/tests/errors/*.تجميع; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .تجميع)
    ok=1
    DIAG_ORDER=file "$AS" "$src" "$OUT/err.o" 2> "$OUT/$name.stderr"
    code=$?
    [ "$code" = 1 ] || { echo "  errors/$name: exit code $code, expected 1"; ok=0; }
    if ! grep -qF -- "$(cat "$ROOT/tests/errors/$name.err")" "$OUT/$name.stderr"; then
        echo "  errors/$name: expected message not found; got:"; sed 's/^/    /' "$OUT/$name.stderr"; ok=0
    fi
    report "errors/$name" $ok
done

echo "assembler tests: $pass passed, $fail failed"
[ $fail = 0 ]
