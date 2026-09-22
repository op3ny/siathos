; echo.asm - app RING 3: imprime seus argumentos (argv[1..]) separados por espaco.
; Entrada (via enter_user / proc_create_user_args):
;   rdi = argc, rsi = argv_va (ponteiro para array de ponteiros de usuario).
; argv_va[i] = ponteiro para a string argv[i]; argv[argc]=NULL.
; PIC (RIP-relative), executa em anel 3 via int 0x80.
bits 64
section .text
global _start

SYS_WRITE equ 1
SYS_EXIT  equ 0

_start:
    mov r12, rsi          ; argv_va
    mov r13, rdi          ; argc
    mov r14, 1            ; i = 1 (pula argv[0]=nome do app)
.loop:
    cmp r14, r13
    jge .done
    mov rax, r12
    mov rsi, [rax + r14*8]   ; argv[i]
    call puts
    lea r15, [r14+1]
    cmp r15, r13
    jge .skipsp
    lea rsi, [rel sps]             ; espaco entre args
    call puts
.skipsp:
    inc r14
    jmp .loop
.done:
    lea rsi, [rel nl]
    call puts
    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80
.hlt: jmp .hlt

; puts(const char *rsi) - escreve string NUL via SYS_WRITE.
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

section .rodata
sps: db " ",0
nl: db 10,0
