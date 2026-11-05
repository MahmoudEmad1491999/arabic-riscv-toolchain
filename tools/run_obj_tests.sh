#!/bin/sh
# Our object format (.كائن) vs ELF: every program of the linker and compiler
# tests is assembled both ways and linked both ways; the two executables must
# be byte-identical. Also: the viewer must accept every object, and damaged
# objects must be rejected (never crash).
set -u
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AS="$ROOT/build/مجمع"
LD="$ROOT/build/رابط"
CC="$ROOT/build/مترجم"
VIEW="$ROOT/build/عارض"
OUT="$ROOT/build/obj-tests"
PREFIX=${RISCV_PREFIX:-riscv64-linux-gnu-}
mkdir -p "$OUT"
pass=0; fail=0
report() { if [ "$2" = 1 ]; then pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }

# same NAME ASM... [-- EXTRA_ELF_OBJ...] : assemble both ways, link both ways, compare
same() {
    name=$1; shift
    elfs=""; nats=""
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do
        base=$(basename "$1" .تجميع)
        "$AS" "$1" "$OUT/$name.$base.o" || return 1
        "$AS" "$1" "$OUT/$name.$base.كائن" || return 1
        elfs="$elfs $OUT/$name.$base.o"; nats="$nats $OUT/$name.$base.كائن"
        shift
    done
    [ $# -gt 0 ] && shift
    # shellcheck disable=SC2086
    "$LD" "$OUT/$name.elf" $elfs "$@" || return 1
    # shellcheck disable=SC2086
    "$LD" "$OUT/$name.nat" $nats "$@" || return 1
    cmp -s "$OUT/$name.elf" "$OUT/$name.nat" || { echo "  $name: executables differ"; return 1; }
    for f in $nats; do
        "$VIEW" "$f" > "$OUT/view.txt" || { echo "  $name: viewer rejected $f"; return 1; }
    done
}

for dir in "$ROOT"/tests/link/*/; do
    dir=${dir%/}
    name=$(basename "$dir")
    [ "$name" = عناوين_خارجية ] && continue
    ok=1
    # shellcheck disable=SC2046
    same "link.$name" $(ls "$dir"/*.تجميع) || ok=0
    report "obj/link/$name" $ok
done

for src in "$ROOT"/tests/asm/*.تجميع; do
    name=$(basename "$src" .تجميع)
    [ -f "$ROOT/tests/asm/$name.norun" ] || continue
    ok=1
    # shellcheck disable=SC2046
    same "cov.$name" "$src" $(ls "$ROOT"/tests/link/عناوين_خارجية/*.تجميع) || ok=0
    report "obj/asm/$name" $ok
done

"$AS" "$ROOT/lib/runtime/تشغيل.تجميع" "$OUT/runtime.o" || exit 1
for src in "$ROOT"/tests/cc/*.غرة; do
    name=$(basename "$src" .غرة)
    ok=1
    extra=""
    if [ -f "$ROOT/tests/cc/$name.c" ]; then
        extra="$OUT/$name.c.o"
        "${PREFIX}gcc" -O1 -march=rv64im -mabi=lp64 -mno-relax -fno-pic -fno-pie -ffreestanding \
            -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib \
            -c "$ROOT/tests/cc/$name.c" -o "$extra" || ok=0
    fi
    if [ $ok = 1 ]; then
        "$CC" "$src" "$OUT/cc.$name.تجميع" "$ROOT/lib" 2> /dev/null || ok=0
    fi
    # the runtime and any GCC object stay ELF: the two formats mix in one link
    if [ $ok = 1 ]; then
        same "cc.$name" "$OUT/cc.$name.تجميع" -- "$OUT/runtime.o" $extra || ok=0
    fi
    report "obj/cc/$name" $ok
done

# our executable format (رابط --برنامج, decision 53)
P="$OUT/program"
mkdir -p "$P"
printf '.شفرة\n.عام _بداية\n_بداية:\n    استدعاء _نظام_اكتب\n    حمل_عنوان وسيط٠، _نظام_اخرج\n    استدعاء _نظام_اخرج\n' > "$P/جيد.تجميع"
printf '.شفرة\n.عام _بداية\n_بداية:\n    استدعاء غير_موجود\n' > "$P/مفقود.تجميع"
printf '.شفرة\n.عام _بداية\n_بداية:\n    قفز _نظام_اخرج\n' > "$P/قفزة.تجميع"
ok=1
for f in جيد مفقود قفزة; do "$AS" "$P/$f.تجميع" "$P/$f.كائن" || ok=0; done
"$LD" --برنامج "$P/جيد" "$P/جيد.كائن" || { echo "  program: link failed"; ok=0; }
"$VIEW" "$P/جيد" > "$P/جيد.txt" || ok=0
grep -q "كائن تنفيذي" "$P/جيد.txt" && grep -q "رمز مستورد _نظام_اكتب" "$P/جيد.txt" &&
    grep -q "إعادة_توطين نسبي_منخفض_ي" "$P/جيد.txt" || { echo "  program: unexpected text form"; ok=0; }
DIAG_ORDER=file "$LD" --برنامج "$P/x" "$P/مفقود.كائن" 2> "$P/err1" && ok=0
grep -qF "مرجع إلى رمز غير معرف" "$P/err1" || { echo "  program: undefined name accepted"; ok=0; }
DIAG_ORDER=file "$LD" --برنامج "$P/x" "$P/قفزة.كائن" 2> "$P/err2" && ok=0
grep -qF "نوع نقل لا يتركه البرنامج للمحمل" "$P/err2" || { echo "  program: jump to an import accepted"; ok=0; }
DIAG_ORDER=file "$LD" "$P/x" "$P/جيد" 2> "$P/err3" && ok=0
grep -qF "ملف كائن غير صالح" "$P/err3" || { echo "  program: executable accepted as input"; ok=0; }
report "obj/program" $ok

# damaged objects and libraries: random bytes changed or cut off; the
# linker, the viewer and the compiler (importing the module) must reject or
# accept, never crash (ASan would abort).
src="$ROOT/tests/link/برنامج_متعدد"
first=$(ls "$src"/*.تجميع | head -1)
"$AS" "$first" "$OUT/victim.كائن" || exit 1
M="$OUT/mut"
rm -rf "$M"; mkdir -p "$M/اختبار_وحدات"
cp "$ROOT/tests/cc/وحدات.غرة" "$M/"
cp "$ROOT/tests/cc/اختبار_وحدات/حساب.غرة" "$M/اختبار_وحدات/"
"$CC" "$M/وحدات.غرة" "$ROOT/lib" || exit 1
"$ROOT/build/محزم" "$OUT/victim.مكتبة" "$M/بناء/غرة/نصوص.كائن" "$M/بناء/تشغيل.تجميع.كائن" || exit 1
ok=1
python3 -I - "$OUT" "$LD" "$VIEW" "$CC" "$ROOT/lib" "$OUT/victim.كائن" "$M/بناء/اختبار_وحدات/حساب.كائن" "$OUT/victim.مكتبة" <<'PY' || ok=0
import os, subprocess, sys, random
out, ld, view, cc, lib = sys.argv[1:6]
victims = sys.argv[6:]
rnd = random.Random(7)
proj = out + '/mutproj'
os.makedirs(proj + '/اختبار_وحدات', exist_ok=True)
open(proj + '/رئيسي.غرة', 'w').write('استورد اختبار_وحدات.حساب؛\nعام دالة رئيسية)(: طبيعي٨ } ارجع ٠؛ {\n')
for vi, path in enumerate(victims):
    data = bytearray(open(path, 'rb').read())
    ext = '.مكتبة' if path.endswith('.مكتبة') else '.كائن'
    for i in range(300):
        d = bytearray(data)
        for _ in range(1 + i % 3):
            d[rnd.randrange(len(d) if i % 2 else min(len(d), 200))] = rnd.randrange(256)
        if i % 7 == 0:
            d = d[:rnd.randrange(len(d))]
        mp = out + '/mut' + ext
        open(mp, 'wb').write(d)
        cmds = [[ld, out + '/mut.out', mp]]
        if ext == '.كائن':
            cmds.append([view, mp])
            if vi == 1:
                open(proj + '/اختبار_وحدات/حساب.كائن', 'wb').write(d)
                cmds.append([cc, proj + '/رئيسي.غرة', lib])
        for cmd in cmds:
            r = subprocess.run(cmd, capture_output=True)
            if r.returncode not in (0, 1, 2) or b'Sanitizer' in r.stderr:
                print('crash', path, i, cmd[0], r.stderr[-400:]); sys.exit(1)
PY
report "obj/mutation" $ok

echo "object format tests: $pass passed, $fail failed"
[ $fail = 0 ]
