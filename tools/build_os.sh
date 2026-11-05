#!/bin/sh
# Builds the operating system (decision 39): first the user programs in
# os/برامج (they are embedded in the kernel image, decision 46), then the
# kernel itself as a bare-metal image.
#   tools/build_os.sh [OS_DIR [ROOT.غرة]]     (default: os/ and نواة.غرة)
# Result: OS_DIR/بناء/<root>. Run it with
#   qemu-system-riscv64 -machine virt -bios none -m 128M -nographic -kernel OS_DIR/بناء/نواة
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="$ROOT/build/مترجم"
OSD=${1:-$ROOT/os}
KERNEL=${2:-نواة.غرة}
cd "$OSD"
for program in برامج/*.غرة; do
    "$CC" "$program" مستخدم
done
"$CC" "$KERNEL" --عاري بدء.تجميع معالج.تجميع برامج_مضمنة.تجميع
