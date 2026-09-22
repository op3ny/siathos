#!/bin/bash
# Cria um disco GPT com particao ESP contendo BOOTX64.EFI e kernel.elf
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
DISK="$BUILD/disk.img"
dd if=/dev/zero of="$DISK" bs=1M count=64 status=none
fdisk "$DISK" >/dev/null 2>&1 <<'EOF'
g
n
1

+40M
t
1
w
EOF
DEV="$(losetup -f --show -P "$DISK")"
echo "loop: $DEV"
mkfs.vfat -F 32 -s 1 -n THAISOS "${DEV}p1" >/dev/null 2>&1
mkdir -p /mnt/thais_efi
mount "${DEV}p1" /mnt/thais_efi
mkdir -p /mnt/thais_efi/EFI/BOOT
cp "$BUILD/BOOTX64.EFI" /mnt/thais_efi/EFI/BOOT/BOOTX64.EFI
cp "$BUILD/kernel.elf" /mnt/thais_efi/kernel.elf
umount /mnt/thais_efi
losetup -d "$DEV"
echo DISKOK
