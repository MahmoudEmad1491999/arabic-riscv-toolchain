#!/bin/sh
# Builds the operating system (decision 39):
#   1. the user programs in os/برامج, in our executable format (decision 53);
#   2. the kernel, a bare-metal image;
#   3. the disk image بناء/قرص.صورة with the programs and the files in
#      os/ملفات (decision 54), made by مهيئ.
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
    "$CC" "$program" مستخدم --برنامج
done
"$CC" "$KERNEL" --عاري بدء.تجميع معالج.تجميع
files=$(for program in برامج/*.غرة; do
            name=$(basename "$program" .غرة)
            printf '%s\n' "برامج/بناء/$name"
        done | LC_ALL=C sort)
# shellcheck disable=SC2086
"$FS" بناء/قرص.صورة $files ملفات/*
