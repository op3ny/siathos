; ps.asm - app RING 3: lista processos ativos via SYS_PROC_LIST (15).
; Layout proc_info_t (align 4): pid@0(u32), name@4[64], state@68(i32),
; is_user@72(u8), exit_code@76(i32). sizeof = 80.
; Entrada: nenhuma (rdi/rsi nao usados). PIC, ring 3 via int 0x80.
bits 64
section .text
global _start

SYS_WRITE     equ 1
SYS_EXIT      equ 0
SYS_PROC_LIST equ 15
PROC_INFO_SZ  equ 80

_start:
    sub rsp, 80*64           ; buffer para ate 64 proc_info_t
    mov rax, SYS_PROC_LIST
    mov rdi, rsp             ; a1 = &info[0]
    mov rsi, 64              ; a2 = max entradas
    int 0x80
    cmp rax, 0xFFFFFFFFFFFFFFFF
    je .fail
    mov r15, rax             ; r15 = numero de processos
    mov r14, rsp             ; r14 = &info[0]
    xor r12, r12             ; i = 0
.loop:
    cmp r12, r15
    jge .done
    mov ebx, [r14 + 0]       ; pid
    mov rsi, rbx
    call putu64
    lea rsi, [rel sps]
    call puts
    lea rsi, [r14 + 4]       ; name[64]
    call puts
    lea rsi, [rel nl]
    call puts
    add r14, PROC_INFO_SZ    ; proxima entrada
    inc r12
    jmp .loop
.done:
    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80
.fail:
    lea rsi, [rel err]
    call puts
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
.hlt: jmp .hlt

; puts(const char *rsi) via SYS_WRITE.
puts:
    mov rax, SYS_WRITE
    mov rdi, 1
    xor rdx, rdx
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

; putu64(u64 rsi) - decimal. Clobbera rax,rcx,rdx,rsi,r8.
putu64:
    mov rax, rsi
    lea r8, [rsp-64]
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
sps: db " ",0
nl:  db 10,0
err: db "ps: falha ao listar processos",10,0
