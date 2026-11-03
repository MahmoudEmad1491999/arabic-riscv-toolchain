#!/bin/sh
# Oracle path: Arabic source -> tools/translit.py -> GNU as/ld -> qemu-riscv64.
# For each tests/asm/NAME.تجميع:
#   NAME.out  (optional) expected stdout
#   NAME.code (optional) expected exit code, default 0
#   NAME.norun (optional) encoding-only test, skipped here
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/oracle"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
mkdir -p "$OUT"
pass=0; fail=0
for src in "$ROOT"/tests/asm/*.تجميع; do
    name=$(basename "$src" .تجميع)
    dir=$(dirname "$src")
    ok=1
    [ -f "$dir/$name.norun" ] && continue
    python3 -I "$ROOT/tools/translit.py" "$src" "$OUT/$name.s" &&
    "${PREFIX}as" -march=rv64i -mabi=lp64 "$OUT/$name.s" -o "$OUT/$name.o" &&
    "${PREFIX}ld" -e _بداية "$OUT/$name.o" -o "$OUT/$name" || ok=0
    if [ $ok = 1 ]; then
        qemu-riscv64 "$OUT/$name" > "$OUT/$name.stdout"
        code=$?
        want=0
        [ -f "$dir/$name.code" ] && want=$(cat "$dir/$name.code")
        [ "$code" = "$want" ] || { echo "  $name: exit code $code, expected $want"; ok=0; }
        if [ -f "$dir/$name.out" ] && ! cmp -s "$dir/$name.out" "$OUT/$name.stdout"; then
            echo "  $name: stdout differs"; ok=0
        fi
    fi
    if [ $ok = 1 ]; then echo "PASS $name"; pass=$((pass+1)); else echo "FAIL $name"; fail=$((fail+1)); fi
done
echo "$pass passed, $fail failed"
[ $fail = 0 ]
