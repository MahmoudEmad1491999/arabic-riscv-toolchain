#!/bin/sh
# مترجم in build mode (decision 37): one command builds بناء/<root>.
#  1. Every tests/cc program builds and runs exactly as in the one-file mode.
#  2. Incremental: nothing is rebuilt when nothing changed; a body change
#     rebuilds only that module; an interface change also rebuilds importers.
#  3. Linking an object built against an old interface is refused.
#  4. A module that exists only as a .كائن is imported through its interface.
#  5. Libraries (.مكتبة, محزم): modules imported from them, and رابط takes
#     only the members that are needed.
set -u
export LC_ALL=C.UTF-8
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
LD="$ROOT/build/رابط"
LIBT="$ROOT/build/محزم"
OUT="$ROOT/build/build-tests"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
rm -rf "$OUT"
mkdir -p "$OUT"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

# run_check DIR NAME EXE : runs EXE, compares with tests/cc/NAME.out / .code
run_check() {
    qemu-riscv64 "$3" > "$1/$2.stdout" 2> /dev/null
    code=$?
    want=0
    [ -f "$ROOT/tests/cc/$2.code" ] && want=$(cat "$ROOT/tests/cc/$2.code")
    [ "$code" = "$want" ] || { echo "  $2: exit code $code, expected $want"; return 1; }
    cmp -s "$ROOT/tests/cc/$2.out" "$1/$2.stdout" || { echo "  $2: stdout differs"; return 1; }
}

# 1. every program
P="$OUT/programs"
cp -r "$ROOT/tests/cc" "$P"
for src in "$P"/*.غرة; do
    name=$(basename "$src" .غرة)
    ok=1
    extra=""
    if [ -f "$P/$name.c" ]; then
        extra="$P/$name.c.o"
        "${PREFIX}gcc" -O1 -march=rv64imfd -mabi=lp64d -mno-relax -fno-pic -fno-pie -ffreestanding \
            -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib \
            -c "$P/$name.c" -o "$extra" || ok=0
    fi
    # shellcheck disable=SC2086
    [ $ok = 1 ] && { "$CC" "$src" "$ROOT/lib" $extra || { echo "  $name: build failed"; ok=0; }; }
    [ $ok = 1 ] && { run_check "$P" "$name" "$P/بناء/$name" || ok=0; }
    report "build/$name" $ok
done

# 2. incremental builds (project: وحدات + اختبار_وحدات/حساب + غرة.نصوص)
I="$OUT/incremental"
mkdir -p "$I/اختبار_وحدات"
cp "$ROOT/tests/cc/وحدات.غرة" "$I/"
cp "$ROOT/tests/cc/اختبار_وحدات/حساب.غرة" "$I/اختبار_وحدات/"
rebuilt() { # lists the module objects newer than the marker, sorted
    find "$I/بناء" -name '*.كائن' ! -name '*.تجميع.كائن' -newer "$I/marker" | sed "s|$I/بناء/||" | sort | tr '\n' ' '
}
step() { # step NAME EXPECTED
    sleep 1; touch "$I/marker"; sleep 1
    ok=1
    "$CC" "$I/وحدات.غرة" "$ROOT/lib" || { echo "  $1: build failed"; ok=0; }
    got=$(rebuilt)
    [ "$got" = "$2" ] || { echo "  $1: rebuilt [$got], expected [$2]"; ok=0; }
    report "build/incremental/$1" $ok
}
"$CC" "$I/وحدات.غرة" "$ROOT/lib" || echo "  first build failed"
step nothing-changed ""
printf '\n# تعليق جديد\n' >> "$I/اختبار_وحدات/حساب.غرة"
step body-changed "اختبار_وحدات/حساب.كائن "
cp "$I/بناء/وحدات.كائن" "$OUT/old.وحدات.كائن"
printf '\nعام ثابت جديد: صحيح٤٦ = ٧؛\n' >> "$I/اختبار_وحدات/حساب.غرة"
step interface-changed "اختبار_وحدات/حساب.كائن وحدات.كائن "
ok=1
run_check "$I" وحدات "$I/بناء/وحدات" || ok=0
report "build/incremental/runs" $ok

# 3. stale object: built against the old interface of اختبار_وحدات.حساب
ok=1
DIAG_ORDER=file "$LD" "$OUT/stale" "$OUT/old.وحدات.كائن" "$I/بناء/اختبار_وحدات/حساب.كائن" \
    "$I/بناء/غرة/نصوص.كائن" "$I/بناء/تشغيل.تجميع.كائن" 2> "$OUT/stale.stderr"
[ $? = 1 ] || { echo "  stale: link did not fail"; ok=0; }
grep -qF "كائن مبني على واجهة قديمة للوحدة: اختبار_وحدات.حساب" "$OUT/stale.stderr" ||
    { echo "  stale: message not found:"; cat "$OUT/stale.stderr"; ok=0; }
report "build/stale-interface" $ok

# 4. a module available only as an object
C="$OUT/compiled"
mkdir -p "$C/اختبار_وحدات"
cp "$ROOT/tests/cc/وحدات.غرة" "$C/"
cp "$P/بناء/اختبار_وحدات/حساب.كائن" "$C/اختبار_وحدات/"
ok=1
"$CC" "$C/وحدات.غرة" "$ROOT/lib" || { echo "  compiled: build failed"; ok=0; }
[ $ok = 1 ] && { run_check "$C" وحدات "$C/بناء/وحدات" || ok=0; }
[ -e "$C/بناء/اختبار_وحدات/حساب.كائن" ] && { echo "  compiled: the module was rebuilt"; ok=0; }
report "build/import-compiled" $ok

# 5. libraries: runtime, غرة.نصوص, اختبار_وحدات.حساب and an unused member
B="$OUT/library"
mkdir -p "$B"
cp "$ROOT/tests/cc/وحدات.غرة" "$B/"
printf '.شفرة\n.عام غير_مستخدم\nغير_مستخدم:\n    عودة\n' > "$OUT/زائد.تجميع"
ok=1
"$ROOT/build/مجمع" "$OUT/زائد.تجميع" "$OUT/زائد.كائن" &&
"$LIBT" "$OUT/مكتبتي.مكتبة" "$P/بناء/تشغيل.تجميع.كائن" "$P/بناء/غرة/نصوص.كائن" \
    "$P/بناء/اختبار_وحدات/حساب.كائن" "$OUT/زائد.كائن" || { echo "  library: محزم failed"; ok=0; }
[ $ok = 1 ] && { "$CC" "$B/وحدات.غرة" "$OUT/مكتبتي.مكتبة" || { echo "  library: build failed"; ok=0; }; }
[ $ok = 1 ] && { run_check "$B" وحدات "$B/بناء/وحدات" || ok=0; }
if [ $ok = 1 ] && "${PREFIX}readelf" -sW "$B/بناء/وحدات" | grep -q 'غير_مستخدم'; then
    echo "  library: unused member was linked"; ok=0
fi
report "build/library" $ok

echo "build tests: $pass passed, $fail failed"
[ $fail = 0 ]
