#!/bin/sh
# Compiler tests: مترجم → مجمع → رابط → qemu-riscv64.
#  1. tests/cc/NAME.غرة: compile (search path lib/), assemble, link with the
#     runtime, run; check NAME.out (stdout, required) and NAME.code (exit
#     code, default 0). Subdirectories of tests/cc hold imported modules.
#  2. tests/cc-errors/NAME.غرة: must fail to compile (exit 1), and stderr in
#     file order (DIAG_ORDER=file) must contain the text in NAME.err.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
AS="$ROOT/build/مجمع"
LD="$ROOT/build/رابط"
OUT="$ROOT/build/cc-tests"
mkdir -p "$OUT"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

"$AS" "$ROOT/lib/runtime/تشغيل.تجميع" "$OUT/runtime.o" || { echo "runtime does not assemble"; exit 1; }

for src in "$ROOT"/tests/cc/*.غرة; do
    name=$(basename "$src" .غرة)
    ok=1
    if DIAG_ORDER=file "$CC" "$src" "$OUT/$name.تجميع" "$ROOT/lib" 2> "$OUT/$name.cc.stderr" &&
       "$AS" "$OUT/$name.تجميع" "$OUT/$name.o" &&
       "$LD" "$OUT/$name" "$OUT/$name.o" "$OUT/runtime.o"; then
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
