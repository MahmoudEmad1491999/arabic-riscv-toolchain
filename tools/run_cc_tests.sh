#!/bin/sh
# Compiler tests: مترجم → مجمع → رابط → qemu-riscv64.
#  1. tests/cc/NAME.غرة: compile (search path lib/), assemble, link with the
#     runtime, run; check NAME.out (stdout, required) and NAME.code (exit
#     code, default 0). Subdirectories of tests/cc hold imported modules.
#     If NAME.c exists it is compiled by GCC and linked in (ABI interop test).
#  2. tests/cc-errors/NAME.غرة: must fail to compile (exit 1), and stderr in
#     file order (DIAG_ORDER=file) must contain the text in NAME.err.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
AS="$ROOT/build/مجمع"
LD="$ROOT/build/رابط"
OUT="$ROOT/build/cc-tests"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
mkdir -p "$OUT"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

"$AS" "$ROOT/lib/runtime/تشغيل.تجميع" "$OUT/runtime.o" || { echo "runtime does not assemble"; exit 1; }

for src in "$ROOT"/tests/cc/*.غرة; do
    name=$(basename "$src" .غرة)
    ok=1
    extra=""
    if [ -f "$ROOT/tests/cc/$name.c" ]; then
        extra="$OUT/$name.c.o"
        "${PREFIX}gcc" -O1 -march=rv64imfd -mabi=lp64d -mno-relax -fno-pic -fno-pie -ffreestanding \
            -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib \
            -c "$ROOT/tests/cc/$name.c" -o "$extra" || ok=0
    fi
    if [ $ok = 1 ] && DIAG_ORDER=file "$CC" "$src" "$OUT/$name.تجميع" "$ROOT/lib" 2> "$OUT/$name.cc.stderr" &&
       "$AS" "$OUT/$name.تجميع" "$OUT/$name.o" &&
       "$LD" "$OUT/$name" "$OUT/$name.o" "$OUT/runtime.o" $extra; then
        qemu-riscv64 "$OUT/$name" > "$OUT/$name.stdout" 2> "$OUT/$name.stderr"
        code=$?
        want=0
        [ -f "$ROOT/tests/cc/$name.code" ] && want=$(cat "$ROOT/tests/cc/$name.code")
        [ "$code" = "$want" ] || { echo "  $name: exit code $code, expected $want"; ok=0; }
        if ! cmp -s "$ROOT/tests/cc/$name.out" "$OUT/$name.stdout"; then
            echo "  $name: stdout differs"; diff "$ROOT/tests/cc/$name.out" "$OUT/$name.stdout" | head -10; ok=0
        fi
    else
        echo "  $name: build failed"; sed 's/^/    /' "$OUT/$name.cc.stderr" | head -5; ok=0
    fi
    report "cc/$name" $ok
done

# the same programs with --تنقيح (A89), assembled into our format: the debug
# rows must be accepted and must not change what the program does
for src in "$ROOT"/tests/cc/*.غرة; do
    name=$(basename "$src" .غرة)
    ok=1
    extra=""
    [ -f "$ROOT/tests/cc/$name.c" ] && extra="$OUT/$name.c.o"
    if DIAG_ORDER=file "$CC" "$src" "$OUT/$name.dbg.تجميع" "$ROOT/lib" --تنقيح 2> "$OUT/$name.dbg.stderr" &&
       "$AS" "$OUT/$name.dbg.تجميع" "$OUT/$name.dbg.كائن" &&
       "$LD" "$OUT/$name.dbg" "$OUT/$name.dbg.كائن" "$OUT/runtime.o" $extra; then
        qemu-riscv64 "$OUT/$name.dbg" > "$OUT/$name.dbg.stdout" 2> /dev/null
        code=$?
        want=0
        [ -f "$ROOT/tests/cc/$name.code" ] && want=$(cat "$ROOT/tests/cc/$name.code")
        [ "$code" = "$want" ] && cmp -s "$ROOT/tests/cc/$name.out" "$OUT/$name.dbg.stdout" || ok=0
    else
        ok=0
    fi
    report "cc/$name --تنقيح" $ok
done

for src in "$ROOT"/tests/cc-errors/*.غرة; do
    name=$(basename "$src" .غرة)
    [ -f "$ROOT/tests/cc-errors/$name.err" ] || continue
    ok=1
    DIAG_ORDER=file "$CC" "$src" "$OUT/err.تجميع" "$ROOT/lib" "$ROOT/tests/cc" 2> "$OUT/err.$name.stderr"
    code=$?
    [ "$code" = 1 ] || { echo "  cc-errors/$name: exit code $code, expected 1"; ok=0; }
    grep -qF -- "$(cat "$ROOT/tests/cc-errors/$name.err")" "$OUT/err.$name.stderr" ||
        { echo "  cc-errors/$name: message not found:"; sed 's/^/    /' "$OUT/err.$name.stderr" | head -3; ok=0; }
    report "cc-errors/$name" $ok
done

echo "compiler tests: $pass passed, $fail failed"
[ $fail = 0 ]
