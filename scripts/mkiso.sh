#!/bin/bash
# ThaÃƒÂ­s OS Ã¢â‚¬â€ cria ISO UEFI com ThaisBoot (bootloader 100% original, sem Limine)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
ESP="$BUILD/esp.img"
ISO="${ISO_OUT:-$BUILD/siath.iso}"
MKISO="$BUILD/mkiso_root"
mkdir -p "$BUILD" "$MKISO"

# 1) BOOTX64.EFI (ThaisBoot) e kernel.elf ja devem existir em $BUILD
if [ ! -f "$BUILD/BOOTX64.EFI" ]; then echo "[mkiso] ERRO: BOOTX64.EFI ausente"; exit 1; fi
if [ ! -f "$BUILD/kernel.elf" ]; then echo "[mkiso] ERRO: kernel.elf ausente"; exit 1; fi

# 2) FAT32 ESP (33MB, clusters de 1 setor -> >= 65525 clusters p/ OVMF aceitar)
rm -f "$ESP"
dd if=/dev/zero of="$ESP" bs=1M count=33 status=none
mkfs.vfat -F 32 -s 1 -n THAISOS "$ESP" >/dev/null 2>&1 || { echo "[mkiso] mkfs.vfat falhou"; exit 1; }

# 3) copia bootloader e kernel para o ESP
mmd -i "$ESP" ::/EFI >/dev/null 2>&1 || true
mmd -i "$ESP" ::/EFI/BOOT >/dev/null 2>&1 || true
mcopy -i "$ESP" "$BUILD/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP" "$BUILD/kernel.elf" ::/kernel.elf
echo "[mkiso] ESP: BOOTX64.EFI + kernel.elf"

# 4) ISO hibrida UEFI (El Torito EFI = ESP). O esp.img deve estar DENTRO da ISO.
cp "$ESP" "$MKISO/efi.img"
# fallback para firmwares que procuram /EFI/BOOT/BOOTX64.EFI direto no ISO9660 (VMware)
mkdir -p "$MKISO/EFI/BOOT"
cp "$BUILD/BOOTX64.EFI" "$MKISO/EFI/BOOT/BOOTX64.EFI"
cp "$BUILD/kernel.elf" "$MKISO/kernel.elf"
rm -f "$ISO"
xorriso -as mkisofs \
  -iso-level 3 \
  -volid "THAISOS" \
  -eltorito-catalog boot.catalog \
  -e efi.img \
  -no-emul-boot \
  -o "$ISO" \
  "$MKISO" >/dev/null 2>&1
echo "[mkiso] ISO: $ISO"
