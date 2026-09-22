# Siath OS — Makefile (Linux nativo: elementary OS / Debian / Ubuntu) — x86_64 UEFI original (ThaisBoot)
# Made with love by Thais (op3n/op3ny)
CC ?= gcc
LD ?= ld
NASM ?= nasm
QEMU ?= qemu-system-x86_64
MKFS_VFAT ?= mkfs.vfat

CFLAGS := -Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -m64 -mcmodel=kernel -march=x86-64 -g -MMD -MP -I src/kernel/include
.DEFAULT_GOAL := all
-include $(wildcard src/kernel/*.d) $(wildcard src/kernel/arch/x86_64/*.d)

# Build limpo (padrao): sem apps de teste. Para incluir os processos de
# demonstracao (workers A/B/C + IPC produtor/consumidor) use: make TEST=1
# Para incluir o demo de RING 3 (Fase K) use: make TEST=1 RING3=1
ifdef TEST
CFLAGS += -DTHAIS_TEST_APPS
endif
ifdef STRESS
CFLAGS += -DTHAIS_TEST_STRESS
endif
ifdef RING3
CFLAGS += -DTHAIS_TEST_RING3
endif
LDFLAGS := -T linker.ld -nostdlib -static

BUILD_DIR := build

BOOT_CFLAGS := -Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -m64 -mcmodel=large -march=x86-64 -I src/boot/thaisboot

# Apps userspace (MARCO 1+): ELF64 compilado FORA do kernel (gcc -fPIC -> PIC,
# relocavel), linkado fixo em 0x200000000000, embutido via genapp.py e
# executado em ring 3 por exec_elf. Cada app tem src/userspace/<nome>/<nome>.c
# e gera src/kernel/<nome>_elf.h (crt0/linkscript compartilhados).
#
# BASE DE MODULOS (ABI 1.2): cada app pode ter src/userspace/<nome>/manifest.cfg
# (nome grego, alias, descricao, kormi). O manifesto vira <nome>_cfg.h embutido
# e e instalado como /bin/<nome>.cfg; se manifest.cfg diz kormi=1, o app tambem
# entra em /kormi e e iniciado antes do login (drivers/servicos).
USER_APPS := $(basename $(notdir $(wildcard src/userspace/*/*.c)))
USR_CRT0 := src/userspace/crt0.asm
USR_LD := src/userspace/userspace.ld
USR_HEADER := src/userspace/spud.h
USR_CFLAGS := -Wall -Wextra -O2 -ffreestanding -fno-builtin -fno-stack-protector -fPIC -mno-red-zone -m64 -march=x86-64 -mcmodel=small
USER_HEADERS := $(addprefix src/kernel/,$(addsuffix _elf.h,$(USER_APPS)))
USER_CFG_HEADERS := $(addprefix src/kernel/,$(addsuffix _cfg.h,$(USER_APPS)))

KERNEL_SRC := $(wildcard src/kernel/*.c) $(wildcard src/kernel/arch/x86_64/*.c)
KERNEL_ASM := $(wildcard src/kernel/arch/x86_64/*.asm)
KERNEL_OBJ := $(KERNEL_SRC:.c=.o) $(KERNEL_ASM:.asm=.o)

KERNEL_ELF := $(BUILD_DIR)/kernel.elf
BOOT_EFI := $(BUILD_DIR)/BOOTX64.EFI
ISO := $(BUILD_DIR)/siath.iso

all: $(USER_HEADERS) $(USER_CFG_HEADERS) $(KERNEL_ELF) $(BOOT_EFI)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ---- Apps userspace (regra generica p/ cada app em USER_APPS) ----
define usr_app_rule
build/$(1).elf: src/userspace/$(1)/$(1).c $$(USR_CRT0) $$(USR_LD) $$(USR_HEADER) | $$(BUILD_DIR)
	nasm -f elf64 $$(USR_CRT0) -o build/$(1)_crt0.o
	$$(CC) $$(USR_CFLAGS) -c src/userspace/$(1)/$(1).c -o build/$(1).o
	$$(LD) -m elf_x86_64 -T $$(USR_LD) -static -no-pie \
		-z noexecstack -z max-page-size=0x1000 \
		build/$(1)_crt0.o build/$(1).o -o $$@
src/kernel/$(1)_elf.h: build/$(1).elf
	python3 scripts/genapp.py build/$(1).elf $(1)_elf src/kernel/$(1)_elf.h
src/kernel/$(1)_cfg.h: src/userspace/$(1)/manifest.cfg
	python3 scripts/genapp.py src/userspace/$(1)/manifest.cfg $(1)_cfg src/kernel/$(1)_cfg.h
endef
$(foreach a,$(USER_APPS),$(eval $(call usr_app_rule,$(a))))

$(KERNEL_OBJ): $(USER_HEADERS) $(USER_CFG_HEADERS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) -f elf64 $< -o $@

# ThaisBoot — bootloader UEFI 100% original (PE32+) via GNU-EFI (PE/ABI corretos)
EFI_INC := /usr/include/efi
EFI_LIB := /usr/lib
BOOT_CFLAGS := -Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-builtin \
	-fPIC -fno-plt -mno-red-zone -maccumulate-outgoing-args -fshort-wchar \
	-m64 -march=x86-64 -DEFI_FUNCTION_WRAPPER -DGNU_EFI_USE_MS_ABI \
	-I src/boot/thaisboot -I $(EFI_INC) -I $(EFI_INC)/x86_64

$(BOOT_EFI): src/boot/thaisboot/main.c | $(BUILD_DIR)
	$(CC) $(BOOT_CFLAGS) -c $< -o $(BUILD_DIR)/thaisboot.o
	$(LD) -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic \
		$(EFI_LIB)/crt0-efi-x86_64.o $(BUILD_DIR)/thaisboot.o \
		-L $(EFI_LIB) -lefi -lgnuefi -o $(BUILD_DIR)/thaisboot.so
	objcopy -j .text -j .text.* -j .sdata -j .data -j .data.* -j .rodata -j .rodata.* -j .bss \
		-j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 --strip-debug $(BUILD_DIR)/thaisboot.so $@

$(KERNEL_ELF): $(KERNEL_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJ) -o $@

# bootloader e kernel -> ISO UEFI
iso: $(KERNEL_ELF) $(BOOT_EFI)
	bash scripts/mkiso.sh

QEMUFLAGS ?=
BOOTLOG := build/boot.log

# Dispositivos USB para testar o caminho xHCI real (teclado em vez de PS/2 puro)
XHCI_DEV := -device qemu-xhci -device usb-kbd
OVMF ?= /usr/share/ovmf/OVMF.fd

run: iso
	$(QEMU) -cdrom $(ISO) -bios $(OVMF) -m 512M -serial stdio -display gtk $(XHCI_DEV) $(QEMUFLAGS)

# Boot UEFI headless (CI/automacao): serial p/ arquivo, sem display.
# PASS = o boot chega ao login ("Novo admin:" na 1a inicializacao).
run-headless: iso
	rm -f $(BOOTLOG)
	timeout 45 $(QEMU) -cdrom $(ISO) -bios $(OVMF) -m 512M \
		-serial file:$(BOOTLOG) -display none -no-reboot $(XHCI_DEV) $(QEMUFLAGS) || true
	@grep -q "Novo admin" $(BOOTLOG) && { echo "[emporion] boot UEFI OK (log: $(BOOTLOG))"; } || { echo "[emporion] FALHA no boot"; tail -20 $(BOOTLOG); exit 1; }

clean:
	rm -rf $(BUILD_DIR) src/kernel/*.o src/kernel/arch/x86_64/*.o $(USER_HEADERS) $(USER_CFG_HEADERS)

# prepara um pendrive UEFI bootavel do Siath OS (WSL Debian, exige sudo):
#   make iso && sudo make usb DEV=/dev/sdX
USB_DEV ?= /dev/sdX
usb: iso
	sudo bash scripts/_mkusb.sh $(USB_DEV)

.PHONY: all iso run run-headless clean usb
