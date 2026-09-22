#!/bin/bash
# _mkusb.sh — prepara um pendrive UEFI bootavel do Siaht OS (no WSL Debian):
#   sudo bash scripts/_mkusb.sh /dev/sdX
# Onde /dev/sdX e o pendrive (CUIDADO: TODOS os dados dele serao apagados).
# Confere seguranca (block device, nao montado, nao /dev/sda sem FORCE).
# Resultado: particao FAT32 'SIAHTOS' com EFI/BOOT/BOOTX64.EFI + kernel.elf.
set -e
DEV="$1"
[ -n "$DEV" ] || { echo "uso: sudo bash scripts/_mkusb.sh /dev/sdX"; exit 2; }
[ "$(id -u)" = "0" ] || { echo "rode com sudo/root (mount + mkfs)."; exit 2; }

case "$DEV" in
  /dev/sd[a-z]|/dev/nvme[0-9]n[0-9]) ;;
  *) echo "dispositivo invalido: $DEV"; exit 2;;
esac
[ -b "$DEV" ] || { echo "nao e block device: $DEV"; exit 2; }
if [ "$DEV" = "/dev/sda" ] && [ "$FORCE" != "1" ]; then
  echo "RECUSEI: $DEV parece o disco do sistema. Se realmente for o pendrive, FORCE=1."; exit 2;
fi
grep -Eq "^$(basename "$DEV")|$DEV " /proc/self/mounts && { echo "pendrive montado — desmonte antes (umount /media/...)"; exit 2; }
command -v sgdisk >/dev/null || { echo "falta sgdisk (gdisk)."; exit 2; }
command -v mkfs.vfat >/dev/null || { echo "falta mkfs.vfat (dosfstools)."; exit 2; }
[ -f build/BOOTX64.EFI ] || { echo "falta build/BOOTX64.EFI — rode 'make iso' antes."; exit 2; }
[ -f build/kernel.elf ] || { echo "falta build/kernel.elf — rode 'make iso' antes."; exit 2; }

echo ">>> APAGANDO $DEV (particao nova FAT32 'SIAHTOS') em 5s... (Ctrl-C aborta)"
sleep 5
sgdisk -Z "$DEV"
sgdisk -n 1:0:0 -t 1:0700 "$DEV"
partprobe "$DEV" || true
sleep 2
PART="${DEV}1"
mkfs.vfat -F 32 -n SIAHTOS "$PART"
MP="$(mktemp -d)"
mount "$PART" "$MP"
mkdir -p "$MP/EFI/BOOT"
cp -f build/BOOTX64.EFI "$MP/EFI/BOOT/BOOTX64.EFI"
cp -f build/kernel.elf "$MP/kernel.elf"
sync
umount "$MP"
rmdir "$MP"
echo "Pendrive Siaht OS pronto em $PART"
echo "  -> boot: selecione no firmware o pendrive (UEFI) + habilite 'Legacy USB'"