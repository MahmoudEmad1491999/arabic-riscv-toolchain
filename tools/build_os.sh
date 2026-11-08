#!/bin/sh
# Builds the operating system (decision 39):
#   1. the user programs in os/برامج, in our executable format (decision 53);
#   2. the kernel, a bare-metal image;
#   3. the toolchain ported to غرة (مصدر/, decision 58), built for our OS: its
#      tools go on the disk in أدوات/, with an example in أمثلة/;
#   4. the disk image بناء/قرص.صورة with the programs and the files in
#      os/ملفات (decision 54), made by مهيئ.
# Programs find the user library in os/مستخدم and the standard library
# (غرة.منصة, غرة.نصوص) in lib/; the start file comes from os/مستخدم.
#   tools/build_os.sh [OS_DIR [ROOT.غرة]]     (default: os/ and نواة.غرة)
# Run it: see docs/spec-os.md, "Running it".
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
FS="$ROOT/build/مهيئ"
OSD=${1:-$ROOT/os}
KERNEL=${2:-نواة.غرة}
cd "$OSD"
for program in برامج/*.غرة; do
    "$CC" "$program" مستخدم "$ROOT/lib" --برنامج
done
"$CC" "$KERNEL" --عاري بدء.تجميع معالج.تجميع
rm -rf بناء/مصدر
mkdir -p بناء
cp -r "$ROOT/مصدر" بناء/مصدر
rm -rf بناء/مصدر/بناء
tools=""
for tool in مجمع رابط عارض محزم مترجم; do
    "$CC" "بناء/مصدر/$tool.غرة" مستخدم "$ROOT/lib" --برنامج
    tools="$tools أدوات/$tool=بناء/مصدر/بناء/$tool"
done
files=$(for program in برامج/*.غرة; do
            name=$(basename "$program" .غرة)
            printf '%s\n' "برامج/بناء/$name"
        done | LC_ALL=C sort)
# shellcheck disable=SC2086
"$FS" بناء/قرص.صورة $files ملفات/* $tools "أمثلة/حساب.تجميع=$ROOT/tests/asm/حساب.تجميع" \
    "أمثلة/صدى.غرة=برامج/صدى.غرة" $(for f in مستخدم/*.غرة مستخدم/runtime/*; do printf '%s=%s ' "$f" "$f"; done)
