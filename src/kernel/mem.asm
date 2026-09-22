; mem.asm - app RING 3: mostra uso de memoria via SYS_SYSINFO.
; PIC (RIP-relative), executado por proc_create_user em anel 3.
bits 64
section .text
global _start

SYS_WRITE   equ 1
SYS_EXIT    equ 0
SYS_SYSINFO equ 12
MIB         equ 1048576

; [rsp+0]=total [rsp+8]=free [rsp+16]=used (u64 cada)
_start:
    sub rsp, 96
    mov rax, SYS_SYSINFO
    mov rdi, rsp
    int 0x80

    lea rsi, [rel hdr]
    call puts
    lea rsi, [rel newl]
    call puts

    lea rsi, [rel s_total]
    call puts
    mov rsi, [rsp+0]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    lea rsi, [rel s_used]
    call puts
    mov rsi, [rsp+16]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    lea rsi, [rel s_free]
    call puts
    mov rsi, [rsp+8]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    ; linha de proporcao
    mov rsi, 0
    call putu64
    lea rsi, [rel newl]
    call puts

    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80
.done:
    jmp .done

putmib:
    mov rax, rsi
    xor edx, edx
    mov rcx, MIB
    div rcx
    mov rsi, rax
    jmp putu64

puts:
    mov rax, SYS_WRITE
    mov rdi, 1
    mov rdx, 0
.len:
    cmp byte [rsi+rdx], 0
    je .sys
    inc rdx
    jmp .len
.sys:
    push rcx
    int 0x80
    pop rcx
    ret

putu64:
    mov rax, rsi
    lea r8, [rsp + 64]
    mov byte [r8], 0
    dec r8
    mov ecx, 10
.next:
    xor edx, edx
    div rcx
    add dl, '0'
    mov [r8], dl
    dec r8
    test rax, rax
    jnz .next
    inc r8
    mov rsi, r8
    call puts
    ret

section .rodata
hdr:
    db "Memoria (Siath OS)",0
s_total:
    db "  total: ",0
s_used:
    db "  usada: ",0
s_free:
    db "  livre: ",0
s_mib:
    db " MiB",0
newl:
    db 10,0
