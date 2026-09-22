; crt0.asm - entrada de app userspace (Siath OS, ring 3) - compartilhado.
; ABI: o _start recebe argc (rdi) e argv (rsi) via enter_user (ctx.arg1/arg2).
; O ELF e linkado em 0x200000000000 (user space) e mapeado por
; proc_create_user_regions no mesmo vaddr (PIC nao e obrigatorio).
bits 64
section .text
global _start
extern main

_start:
    and rsp, -16
    call main
    mov rdi, rax
    mov rax, 0          ; SYS_EXIT
    int 0x80
.hlt:
    jmp .hlt

section .note.GNU-stack noalloc noexec nowrite progbits