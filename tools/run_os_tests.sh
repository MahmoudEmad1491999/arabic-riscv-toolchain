#!/bin/sh
# The operating system (decision 39): bare-metal images built by
# tools/build_os.sh (user programs first, then the kernel with them embedded),
# run on qemu-system-riscv64 -machine virt -bios none.
#  1. os/نواة.غرة, the kernel itself: tests/os/نواة.out, exit code 0.
#  2. tests/os/NAME.غرة: small test kernels using the os/ modules;
#     NAME.out is the serial output, NAME.code the exit code (default 0).
# QEMU's exit code is the one the kernel gives the test device.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
OUT="$ROOT/build/os-tests"
QEMU=${QEMU_SYSTEM:-qemu-system-riscv64}
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$ROOT/os" "$OUT/os"
cp "$ROOT"/tests/os/*.غرة "$OUT/os/"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

for src in "$OUT"/os/*.غرة; do
    name=$(basename "$src" .غرة)
    [ -f "$ROOT/tests/os/$name.out" ] || continue
    ok=1
    DIAG_ORDER=file "$ROOT/tools/build_os.sh" "$OUT/os" "$name.غرة" > "$OUT/$name.build" 2>&1 ||
        { echo "  $name: build failed"; sed 's/^/    /' "$OUT/$name.build" | head -5; ok=0; }
    if [ $ok = 1 ]; then
        timeout 30 "$QEMU" -machine virt -bios none -m 128M -nographic -monitor none \
            -kernel "$OUT/os/بناء/$name" > "$OUT/$name.stdout" 2> "$OUT/$name.stderr" < /dev/null
        code=$?
        want=0
        [ -f "$ROOT/tests/os/$name.code" ] && want=$(cat "$ROOT/tests/os/$name.code")
        [ "$code" = "$want" ] || { echo "  $name: exit code $code, expected $want"; ok=0; }
        cmp -s "$ROOT/tests/os/$name.out" "$OUT/$name.stdout" ||
            { echo "  $name: serial output differs"; diff "$ROOT/tests/os/$name.out" "$OUT/$name.stdout" | head -5; ok=0; }
    fi
    report "os/$name" $ok
done

echo "os tests: $pass passed, $fail failed"
[ $fail = 0 ]
