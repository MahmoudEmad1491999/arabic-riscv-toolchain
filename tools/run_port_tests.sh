#!/bin/sh
# Phase 6 port tests (decisions 56-57): the code in مصدر/, written in غرة,
# against the C code it translates.
#  1. tests/port/اختبار_المشترك.غرة: the unit checks of src/common (the same as
#     tests/unit/test_common.c), then every object and library made below is
#     parsed and written again: it must come back byte for byte.
#  2. tests/port/اختبار_التشخيص.غرة and tests/port/تشخيص.c make the same calls
#     to the diagnostics: standard error must be identical, in display order
#     and with DIAG_ORDER=file.
#  3. Every tool ported so far against its C original: same standard output,
#     standard error and exit code, on every object made below, on damaged
#     objects, and on the error cases.
#  6. مترجم: the same assembly for every compiler test, the OS and the ported
#     tools; the same errors for the error tests (both orders) and for damaged
#     sources; in build mode the same objects and executables for every
#     compiler test, the kernel (--عاري) and an OS program (--برنامج); and it
#     builds itself to the same files as the C compiler (stage 2 = stage 3).
#  5. رابط: the same executables, in all three modes, from the link tests (our
#     objects and ELF), the compiler tests with the library, an OS program, the
#     same errors for the error tests, and damaged objects.
#  4. مجمع: the same object files, in both formats, from every assembly file
#     of the tests, the OS and the runtimes, from the compiler's output for the
#     compiler tests, the OS and the ported tools themselves, and the same
#     errors for the error tests and for damaged assembly sources.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
AS="$ROOT/build/مجمع"
LIB="$ROOT/build/محزم"
OUT="$ROOT/build/port-tests"
rm -rf "$OUT"
mkdir -p "$OUT/objs" "$OUT/cc"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

# the ported tools, built by our compiler for Linux
for tool in عارض محزم مجمع رابط مترجم; do
    "$CC" "$ROOT/مصدر/$tool.غرة" "$ROOT/lib" > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; echo "FAIL build مصدر/$tool"; exit 1; }
done
VIEW_C="$ROOT/build/عارض"
VIEW_G="$ROOT/مصدر/بناء/عارض"
LIB_C="$LIB"
LIB_G="$ROOT/مصدر/بناء/محزم"

# objects of every kind: assembled, compiled with interfaces, a library, an executable
for src in "$ROOT"/tests/asm/*.تجميع "$ROOT"/tests/link/*/*.تجميع; do
    name=$(basename "$(dirname "$src")")_$(basename "$src" .تجميع)
    "$AS" "$src" "$OUT/objs/$name.كائن" 2>/dev/null
done
cp -r "$ROOT"/tests/cc/* "$OUT/cc/"
for src in "$OUT"/cc/*.غرة; do
    "$CC" "$src" "$ROOT/lib" > /dev/null 2>&1
done
"$LIB" "$OUT/objs/مكتبة.مكتبة" "$OUT"/cc/بناء/غرة/*.كائن "$OUT"/cc/بناء/تشغيل.تجميع.كائن
mkdir -p "$OUT/os"
cp "$ROOT/os/برامج/صدى.غرة" "$OUT/os/"
(cd "$OUT/os" && "$CC" صدى.غرة "$ROOT/os/مستخدم" --برنامج > /dev/null 2>&1)
files=$( (find "$OUT/objs" "$OUT/cc" "$OUT/os" -name '*.كائن' -o -name '*.مكتبة'; echo "$OUT/os/بناء/صدى") | LC_ALL=C sort)
objects=$(printf '%s\n' $files | grep -v '\.مكتبة$')
printf '%s\n' $files | grep -q '\.مكتبة$' || { echo "FAIL no library made"; fail=$((fail+1)); }
printf '%s\n' $files > "$OUT/files.txt"

# 1. unit checks and round trips
build_test() {
    cp "$ROOT/tests/port/$1.غرة" "$OUT/"
    DIAG_ORDER=file "$CC" "$OUT/$1.غرة" "$ROOT/مصدر" "$ROOT/lib" > "$OUT/$1.build" 2>&1 ||
        { echo "  $1: build failed"; head -5 "$OUT/$1.build"; return 1; }
}
ok=1
build_test اختبار_المشترك || ok=0
if [ $ok = 1 ]; then
    # shellcheck disable=SC2086
    qemu-riscv64 "$OUT/بناء/اختبار_المشترك" $files > "$OUT/common.out" 2>&1 || ok=0
    cmp -s "$ROOT/tests/port/اختبار_المشترك.out" "$OUT/common.out" ||
        { echo "  اختبار_المشترك: output differs"; diff "$ROOT/tests/port/اختبار_المشترك.out" "$OUT/common.out" | head -5; ok=0; }
fi
report "port/مشترك ($(printf '%s\n' $files | wc -l) files)" $ok

# 2. diagnostics, C and غرة
ok=1
cc -std=c11 -o "$OUT/diag_c" "$ROOT/tests/port/تشخيص.c" "$ROOT"/src/common/diag.c "$ROOT"/src/common/rt.c \
    "$ROOT"/src/common/utf8.c "$ROOT"/src/common/number.c "$ROOT"/src/common/chars.c || ok=0
build_test اختبار_التشخيص || ok=0
for order in "" file; do
    [ $ok = 1 ] || break
    DIAG_ORDER=$order "$OUT/diag_c" 2> "$OUT/diag_c.err"; a=$?
    DIAG_ORDER=$order qemu-riscv64 "$OUT/بناء/اختبار_التشخيص" 2> "$OUT/diag_g.err"; b=$?
    [ $a = 0 ] && [ $b = 0 ] && cmp -s "$OUT/diag_c.err" "$OUT/diag_g.err" ||
        { echo "  diagnostics (DIAG_ORDER=$order): exit $a/$b"; diff "$OUT/diag_c.err" "$OUT/diag_g.err" | head -5; ok=0; }
done
report "port/تشخيص" $ok

# 3. the tools: same stdout, stderr and exit code
same() {   # same NAME C-TOOL غرة-TOOL ARGS...: 1 if both behave identically
    n=$1; c=$2; g=$3; shift 3
    "$c" "$@" > "$OUT/c.out" 2> "$OUT/c.err"; a=$?
    qemu-riscv64 "$g" "$@" > "$OUT/g.out" 2> "$OUT/g.err"; b=$?
    if [ $a = $b ] && cmp -s "$OUT/c.out" "$OUT/g.out" && cmp -s "$OUT/c.err" "$OUT/g.err"; then
        return 0
    fi
    echo "  $n differs on: $*"; diff "$OUT/c.out" "$OUT/g.out" | head -3; diff "$OUT/c.err" "$OUT/g.err" | head -3
    return 1
}
ok=1
for f in $objects; do same عارض "$VIEW_C" "$VIEW_G" "$f" || ok=0; done
head -c 100 "$CC" > "$OUT/bad.كائن"
for order in "" file; do
    export DIAG_ORDER=$order
    same عارض "$VIEW_C" "$VIEW_G" || ok=0
    same عارض "$VIEW_C" "$VIEW_G" "$OUT/لا_يوجد.كائن" || ok=0
    same عارض "$VIEW_C" "$VIEW_G" "$OUT/bad.كائن" || ok=0
    same عارض "$VIEW_C" "$VIEW_G" "$OUT/objs/مكتبة.مكتبة" || ok=0
    same عارض "$VIEW_C" "$VIEW_G" أ ب || ok=0
done
unset DIAG_ORDER
# damaged objects: random bytes changed or cut off
victims=$(printf '%s\n' $objects | grep -E 'بناء/(وحدات|اختبار_وحدات/حساب|صدى)\.كائن$|objs/link_' | head -4)
# shellcheck disable=SC2086
python3 -I - "$OUT" $victims <<'PY' > "$OUT/mutants.txt" || ok=0
import random, sys
out, victims = sys.argv[1], sys.argv[2:]
rnd = random.Random(11)
n = 0
for path in victims:
    data = bytearray(open(path, 'rb').read())
    for i in range(60):
        d = bytearray(data)
        for _ in range(1 + i % 3):
            d[rnd.randrange(len(d) if i % 2 else min(len(d), 200))] = rnd.randrange(256)
        if i % 7 == 0:
            d = d[:rnd.randrange(len(d))]
        name = '%s/mut%d.كائن' % (out, n)
        open(name, 'wb').write(d)
        print(name)
        n += 1
PY
for f in $(cat "$OUT/mutants.txt"); do same عارض "$VIEW_C" "$VIEW_G" "$f" || ok=0; done
report "port/عارض ($(printf '%s\n' $objects | wc -l) objects, $(wc -l < "$OUT/mutants.txt") damaged)" $ok

# محزم: the same library, byte for byte, and the same errors
ok=1
lib_same() {   # lib_same ARGS... (the output path is added by us)
    "$LIB_C" "$OUT/c.مكتبة" "$@" > "$OUT/c.out" 2> "$OUT/c.err"; a=$?
    qemu-riscv64 "$LIB_G" "$OUT/g.مكتبة" "$@" > "$OUT/g.out" 2> "$OUT/g.err"; b=$?
    if [ $a = $b ] && cmp -s "$OUT/c.out" "$OUT/g.out" && cmp -s "$OUT/c.err" "$OUT/g.err" &&
       { [ $a != 0 ] || cmp -s "$OUT/c.مكتبة" "$OUT/g.مكتبة"; }; then
        rm -f "$OUT/c.مكتبة" "$OUT/g.مكتبة"
        return 0
    fi
    echo "  محزم differs on: $*"; diff "$OUT/c.err" "$OUT/g.err" | head -3
    return 1
}
# shellcheck disable=SC2086
lib_same $(printf '%s\n' $objects | head -12) || ok=0
# shellcheck disable=SC2086
lib_same $objects || ok=0
for order in "" file; do
    export DIAG_ORDER=$order
    lib_same || ok=0
    lib_same "$OUT/لا_يوجد.كائن" || ok=0
    lib_same "$OUT/bad.كائن" "$OUT/mut3.كائن" || ok=0
    "$LIB_C" "$OUT" $(printf '%s\n' $objects | head -1) > /dev/null 2> "$OUT/c.err"; a=$?
    qemu-riscv64 "$LIB_G" "$OUT" $(printf '%s\n' $objects | head -1) > /dev/null 2> "$OUT/g.err"; b=$?
    [ $a = $b ] && cmp -s "$OUT/c.err" "$OUT/g.err" || { echo "  محزم differs on an unwritable output"; ok=0; }
done
unset DIAG_ORDER
report "port/محزم" $ok

# مجمع: the same objects and the same errors
ok=1
AS_G="$ROOT/مصدر/بناء/مجمع"
mkdir -p "$OUT/asm"
as_same() {   # as_same SOURCE EXT: both assemblers, same output file, stdout, stderr and exit code
    "$AS" "$1" "$OUT/asm/c.$2" > "$OUT/c.out" 2> "$OUT/c.err"; a=$?
    qemu-riscv64 "$AS_G" "$1" "$OUT/asm/g.$2" > "$OUT/g.out" 2> "$OUT/g.err"; b=$?
    if [ $a = $b ] && cmp -s "$OUT/c.out" "$OUT/g.out" && cmp -s "$OUT/c.err" "$OUT/g.err" &&
       { [ $a != 0 ] || cmp -s "$OUT/asm/c.$2" "$OUT/asm/g.$2"; }; then
        rm -f "$OUT/asm/c.$2" "$OUT/asm/g.$2"
        return 0
    fi
    echo "  مجمع differs on $1 ($2): exit $a/$b"; diff "$OUT/c.err" "$OUT/g.err" | head -3
    return 1
}
# the compiler's output: the compiler tests, the OS, the ported tools
for src in "$ROOT"/tests/cc/*.غرة; do
    "$CC" "$src" "$OUT/asm/cc_$(basename "$src" .غرة).تجميع" "$ROOT/lib" 2>/dev/null
done
(cd "$ROOT/os" && "$CC" نواة.غرة "$OUT/asm/os_نواة.تجميع" "$ROOT/lib" 2>/dev/null
 for p in برامج/*.غرة; do "$CC" "$p" "$OUT/asm/os_$(basename "$p" .غرة).تجميع" مستخدم "$ROOT/lib" 2>/dev/null; done)
for tool in عارض محزم مجمع رابط; do "$CC" "$ROOT/مصدر/$tool.غرة" "$OUT/asm/tool_$tool.تجميع" "$ROOT/lib"; done
sources=$(ls "$ROOT"/tests/asm/*.تجميع "$ROOT"/tests/link/*/*.تجميع "$ROOT"/os/*.تجميع "$ROOT"/lib/runtime/*.تجميع \
             "$ROOT"/os/مستخدم/runtime/*.تجميع "$OUT"/asm/*.تجميع)
nsrc=0
for src in $sources; do
    nsrc=$((nsrc+1))
    as_same "$src" o || ok=0
    as_same "$src" كائن || ok=0
done
nerr=0
for order in "" file; do
    export DIAG_ORDER=$order
    for src in "$ROOT"/tests/errors/*.تجميع; do nerr=$((nerr+1)); as_same "$src" كائن || ok=0; done
    "$AS" > "$OUT/c.err" 2>&1; a=$?; qemu-riscv64 "$AS_G" > "$OUT/g.err" 2>&1; b=$?
    [ $a = $b ] && cmp -s "$OUT/c.err" "$OUT/g.err" || { echo "  مجمع differs on usage"; ok=0; }
    as_same "$OUT/لا_يوجد.تجميع" o || ok=0
    "$AS" "$ROOT/tests/asm/حساب.تجميع" "$OUT/لا_دليل/خرج.o" > "$OUT/c.err" 2>&1; a=$?
    qemu-riscv64 "$AS_G" "$ROOT/tests/asm/حساب.تجميع" "$OUT/لا_دليل/خرج.o" > "$OUT/g.err" 2>&1; b=$?
    [ $a = $b ] && cmp -s "$OUT/c.err" "$OUT/g.err" || { echo "  مجمع differs on an unwritable output"; ok=0; }
done
unset DIAG_ORDER
# damaged sources: characters deleted, doubled or replaced, lines swapped
python3 -I - "$OUT/asm" "$ROOT"/tests/asm/*.تجميع <<'PY' > "$OUT/asm_mutants.txt" || ok=0
import random, sys
out, srcs = sys.argv[1], sys.argv[2:]
rnd = random.Random(5)
pool = list('٠١٢٣٤٥٦٧٨٩،:+-*/)(][ \n"\'ـabc,؛') + ['سجل٩', 'وسيط٠', 'ق', 'م', '.عام', 'عالي:', '٠س']
n = 0
for path in srcs:
    text = open(path, encoding='utf-8').read()
    for i in range(25):
        t = list(text)
        for _ in range(1 + i % 3):
            k = rnd.randrange(len(t))
            op = rnd.randrange(3)
            if op == 0: del t[k]
            elif op == 1: t.insert(k, t[k])
            else: t[k] = rnd.choice(pool)
        t = ''.join(t)
        if i % 5 == 0:
            lines = t.split('\n'); a, b = rnd.randrange(len(lines)), rnd.randrange(len(lines))
            lines[a], lines[b] = lines[b], lines[a]; t = '\n'.join(lines)
        name = '%s/mut%d.تجميع' % (out, n)
        open(name, 'w', encoding='utf-8').write(t)
        print(name)
        n += 1
PY
for src in $(cat "$OUT/asm_mutants.txt"); do as_same "$src" كائن || ok=0; done
report "port/مجمع ($nsrc sources × 2 formats, $nerr error tests, $(wc -l < "$OUT/asm_mutants.txt") damaged sources)" $ok

# رابط: the same executables and the same errors
ok=1
LD_C="$ROOT/build/رابط"
LD_G="$ROOT/مصدر/بناء/رابط"
mkdir -p "$OUT/ld"
ld_same() {   # ld_same ARGS...: both linkers (output path added first), same file, stdout, stderr, exit
    m=""; case "${1:-}" in --*) m=$1; shift;; esac
    "$LD_C" $m "$OUT/ld/c" "$@" > "$OUT/c.out" 2> "$OUT/c.err"; a=$?
    qemu-riscv64 "$LD_G" $m "$OUT/ld/g" "$@" > "$OUT/g.out" 2> "$OUT/g.err"; b=$?
    if [ $a = $b ] && cmp -s "$OUT/c.out" "$OUT/g.out" && cmp -s "$OUT/c.err" "$OUT/g.err" &&
       { [ $a != 0 ] || cmp -s "$OUT/ld/c" "$OUT/ld/g"; }; then
        rm -f "$OUT/ld/c" "$OUT/ld/g"; nld=$((nld+1))
        return 0
    fi
    echo "  رابط differs on: $m $*: exit $a/$b"; diff "$OUT/c.err" "$OUT/g.err" | head -3
    return 1
}
nld=0
for order in "" file; do
    export DIAG_ORDER=$order
    for d in "$ROOT"/tests/link/*/ "$ROOT"/tests/link-errors/*/; do
        srcs=$(ls "$d"*.تجميع | LC_ALL=C sort); objs=""; elfs=""
        for s in $srcs; do
            b=$OUT/ld/$(basename "$d")_$(basename "$s" .تجميع)
            "$AS" "$s" "$b.كائن" 2>/dev/null; "$AS" "$s" "$b.o" 2>/dev/null
            objs="$objs $b.كائن"; elfs="$elfs $b.o"
        done
        for mode in "" --عاري --برنامج; do
            # shellcheck disable=SC2086
            ld_same $mode $objs || ok=0
            # shellcheck disable=SC2086
            ld_same $mode $elfs || ok=0
        done
    done
    for src in "$OUT"/cc/*.غرة; do
        o="$OUT/cc/بناء/$(basename "$src" .غرة).كائن"
        [ -f "$o" ] && { ld_same "$o" "$OUT/cc/بناء/تشغيل.تجميع.كائن" "$OUT/objs/مكتبة.مكتبة" || ok=0; }
    done
    # shellcheck disable=SC2046
    ld_same --برنامج $(find "$OUT/os/بناء" -name '*.كائن' | LC_ALL=C sort) || ok=0
    for tool in مجمع رابط; do   # the ported tools themselves, linked again
        # shellcheck disable=SC2046
        ld_same "$ROOT/مصدر/بناء/$tool.كائن" $(cd "$ROOT/مصدر/بناء" && find التجميع الربط مشترك غرة -name '*.كائن' 2>/dev/null |
            LC_ALL=C sort | sed "s|^|$ROOT/مصدر/بناء/|") "$ROOT/مصدر/بناء/تشغيل.تجميع.كائن" || ok=0
    done
    ld_same || ok=0
    ld_same --برنامج || ok=0
    ld_same "$OUT/لا_يوجد.كائن" || ok=0
    ld_same "$OUT/bad.كائن" || ok=0
    ld_same "$ROOT/README.md" || ok=0
done
unset DIAG_ORDER
"$LD_C" "$OUT/ld/x/y" "$OUT/objs/asm_"*.كائن > /dev/null 2> "$OUT/c.err"; a=$?
qemu-riscv64 "$LD_G" "$OUT/ld/x/y" "$OUT/objs/asm_"*.كائن > /dev/null 2> "$OUT/g.err"; b=$?
[ $a = $b ] && cmp -s "$OUT/c.err" "$OUT/g.err" || { echo "  رابط differs on an unwritable output"; ok=0; }
# damaged objects, our format and ELF: each linked with the runtime
for f in $(cat "$OUT/mutants.txt"); do ld_same "$f" "$OUT/cc/بناء/تشغيل.تجميع.كائن" || ok=0; done
python3 -I - "$OUT/ld" "$OUT"/ld/*.o <<'PY' > "$OUT/ld_mutants.txt" || ok=0
import random, sys
out, victims = sys.argv[1], sys.argv[2:6]
rnd = random.Random(7)
n = 0
for path in victims:
    data = bytearray(open(path, 'rb').read())
    for i in range(40):
        d = bytearray(data)
        for _ in range(1 + i % 3):
            d[rnd.randrange(len(d))] = rnd.randrange(256)
        if i % 7 == 0:
            d = d[:rnd.randrange(len(d))]
        name = '%s/mut%d.o' % (out, n)
        open(name, 'wb').write(d)
        print(name)
        n += 1
PY
for f in $(cat "$OUT/ld_mutants.txt"); do ld_same "$f" || ok=0; done
report "port/رابط ($nld links, $(($(wc -l < "$OUT/mutants.txt") + $(wc -l < "$OUT/ld_mutants.txt"))) damaged objects)" $ok

# مترجم: the same assembly, objects, executables and errors
ok=1
CC_G="$ROOT/مصدر/بناء/مترجم"
C="$OUT/cc-port"
mkdir -p "$C"
ncc=0
cc_same() {   # cc_same DIR ARGS...: both compilers in copies DIR/c and DIR/g, same stdout, stderr, exit and files
    d=$1; shift
    (cd "$d/c" && "$CC" "$@" > ../c.out 2> ../c.err); a=$?
    (cd "$d/g" && qemu-riscv64 "$CC_G" "$@" > ../g.out 2> ../g.err); b=$?
    if [ $a = $b ] && cmp -s "$d/c.out" "$d/g.out" && cmp -s "$d/c.err" "$d/g.err" && diff -r "$d/c" "$d/g" > /dev/null; then
        ncc=$((ncc+1)); return 0
    fi
    echo "  مترجم differs in $d on: $*: exit $a/$b"; diff "$d/c.err" "$d/g.err" | head -3; diff -rq "$d/c" "$d/g" | head -3
    return 1
}
fresh() {   # fresh DIR SOURCE-DIR...: DIR/c and DIR/g, copies of the sources
    rm -rf "$1"; mkdir -p "$1/c" "$1/g"; d=$1; shift
    for s in "$@"; do cp -r "$s"/. "$d/c/"; cp -r "$s"/. "$d/g/"; done
    rm -rf "$d/c/بناء" "$d/g/بناء"
}
# one assembly file per program
fresh "$C/asm" "$ROOT/tests/cc"
for src in "$ROOT"/tests/cc/*.غرة; do cc_same "$C/asm" "$(basename "$src")" out.تجميع "$ROOT/lib" || ok=0; done
fresh "$C/os" "$ROOT/os"
for src in "$ROOT"/os/برامج/*.غرة; do cc_same "$C/os" "برامج/$(basename "$src")" out.تجميع مستخدم "$ROOT/lib" || ok=0; done
cc_same "$C/os" نواة.غرة out.تجميع || ok=0
fresh "$C/tools" "$ROOT/مصدر"
for tool in عارض محزم مجمع رابط مترجم; do cc_same "$C/tools" $tool.غرة out.تجميع "$ROOT/lib" || ok=0; done
# errors, both orders; usage and file errors
fresh "$C/err" "$ROOT/tests/cc-errors"
for order in "" file; do
    export DIAG_ORDER=$order
    for src in "$ROOT"/tests/cc-errors/*.غرة; do cc_same "$C/err" "$(basename "$src")" out.تجميع "$ROOT/lib" || ok=0; done
    cc_same "$C/err" || ok=0
    cc_same "$C/err" لا_يوجد.غرة out.تجميع || ok=0
    cc_same "$C/err" "$ROOT/README.md" || ok=0
    cc_same "$C/err" 1سيئ.غرة "$ROOT/lib" || ok=0
done
unset DIAG_ORDER
rm -f "$C"/err/*/out.تجميع
# build mode: every program, the kernel, an OS program, and the compiler itself
fresh "$C/build" "$ROOT/tests/cc"
for src in "$ROOT"/tests/cc/*.غرة; do
    extra=""; name=$(basename "$src" .غرة)
    [ -f "$ROOT/tests/cc/$name.c" ] && continue   # needs a C object (run_build_tests.sh)
    cc_same "$C/build" "$name.غرة" "$ROOT/lib" $extra || ok=0
done
fresh "$C/kernel" "$ROOT/os"
cc_same "$C/kernel" نواة.غرة --عاري بدء.تجميع معالج.تجميع || ok=0
cc_same "$C/kernel" برامج/صدى.غرة مستخدم "$ROOT/lib" --برنامج || ok=0
fresh "$C/self" "$ROOT/مصدر"
cc_same "$C/self" مترجم.غرة "$ROOT/lib" || ok=0
# damaged sources: characters deleted, doubled or replaced, lines swapped
mkdir -p "$C/mut/c" "$C/mut/g"
python3 -I - "$C/mut" "$ROOT"/tests/cc/أساسيات.غرة "$ROOT"/tests/cc/بيانات_وتحكم.غرة "$ROOT"/مصدر/الترجمة/أنواع.غرة <<'PY' > "$C/mutants.txt" || ok=0
import random, sys
out, srcs = sys.argv[1], sys.argv[2:]
rnd = random.Random(9)
pool = list('٠١٢٣٤٥٦٧٨٩،:+-*/)(][}{ \n"\'^@.؛=<>') + ['ليس', 'و', 'أو', 'متغير', 'دالة', 'حجم', 'صحيح٢٣', 'اختياري', 'وجد', 'لاشيء']
n = 0
for path in srcs:
    text = open(path, encoding='utf-8').read()
    for i in range(40):
        t = list(text)
        for _ in range(1 + i % 3):
            k = rnd.randrange(len(t))
            op = rnd.randrange(3)
            if op == 0: del t[k]
            elif op == 1: t.insert(k, t[k])
            else: t[k] = rnd.choice(pool)
        t = ''.join(t)
        if i % 5 == 0:
            lines = t.split('\n'); a, b = rnd.randrange(len(lines)), rnd.randrange(len(lines))
            lines[a], lines[b] = lines[b], lines[a]; t = '\n'.join(lines)
        for side in 'cg':
            open('%s/%s/م%d.غرة' % (out, side, n), 'w', encoding='utf-8').write(t)
        print('م%d.غرة' % n)
        n += 1
PY
for f in $(cat "$C/mutants.txt"); do cc_same "$C/mut" "$f" out.تجميع "$ROOT/lib" || ok=0; done
report "port/مترجم ($ncc runs, $(wc -l < "$C/mutants.txt") damaged sources; builds itself identically)" $ok

echo "port tests: $pass passed, $fail failed"
[ $fail = 0 ]
