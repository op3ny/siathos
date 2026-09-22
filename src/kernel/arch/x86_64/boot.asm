; Siath OS - entry point x86_64 (ThaisBoot)
; Made with love by Thais (op3n/op3ny)
bits 64
section .text
global _start
extern thais_main
extern serial_init
_start:
    cli
    mov r15, rdi
    mov rsp, stack_top
    xor rbp, rbp
    call serial_init
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    mov rsi, bootmsg
.ploop:
    lodsb
    test al, al
    jz .done
    call serial_putc_asm
    jmp .ploop
.done:
    mov rdi, r15
    call thais_main
.halt:
    hlt
    jmp .halt

section .rodata
bootmsg db "THAIS_BOOT_OK", 13, 10, 0

section .text
serial_putc_asm:
    push ax
    mov dx, 0x3F8
    out dx, al
    pop ax
    ret

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: