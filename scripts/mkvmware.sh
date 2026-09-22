#!/bin/bash
# Thais OS - cria disco GPT+ESP para VMware
set -e
ROOT=$(cd $(dirname "$0")/.. && pwd)
BUILD="$ROOT/build"
ESP="$BUILD/esp.img"
DISK_RAW="$BUILD/thais-disk.img"
DISK_VMDK="$BUILD/thais-disk.vmdk"
if [ ! -f "$ESP" ]; then echo "mkvmware esp ausente"; exit 1; fi
rm -f "$DISK_RAW"
dd if=/dev/zero of="$DISK_RAW" bs=1M count=64 status=none
parted -s "$DISK_RAW" mklabel gpt
parted -s "$DISK_RAW" mkpart ESP fat32 1MiB 34MiB
parted -s "$DISK_RAW" set 1 boot on
parted -s "$DISK_RAW" set 1 esp on
dd if="$ESP" of="$DISK_RAW" bs=512 seek=2048 conv=notrunc status=none
qemu-img convert -f raw -O vmdk "$DISK_RAW" "$DISK_VMDK"
echo "RAW OK"
echo "VMDK OK"
ls -lh "$DISK_RAW" "$DISK_VMDK"
