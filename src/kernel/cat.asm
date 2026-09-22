; cat.asm - app RING 3: imprime o conteudo de um arquivo via SYS_FS_READ.
; Entrada: rdi=argc, rsi=argv_va. arquivo = argv[1] (obrigatorio).
; Buffer na STACK (mapeada RW). PIC (RIP-relative).
bits 64
section .text
global _start
SYS_FS_READ equ 7
SYS_WRITE   equ 1
SYS_EXIT    equ 0

_start:
    mov r12, rsi            ; argv_va
    mov r13, rdi            ; argc
    cmp r13, 2
    jl .usage
    mov rax, r12
    mov rbx, [rax + 8]      ; argv[1] = caminho do arquivo
    sub rsp, 4096           ; buffer na stack
    mov r15, rsp
    mov rax, SYS_FS_READ
    mov rdi, rbx            ; a1 = path
    mov rsi, r15            ; a2 = buf
    mov rdx, 4095           ; a3 = max
    int 0x80
    ; rax = bytes lidos (ou -1)
    cmp rax, -1
    je .err
    ; imprime N bytes (rax)
    mov r13, rax            ; n bytes
    mov rax, SYS_WRITE
    mov rdi, 1
    mov rsi, r15
    mov rdx, r13
    int 0x80
    ; garante nova linha se o arquivo nao termina nela
    lea rsi,[rel nl]
    call puts
.done:
    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80
    jmp .done
.err:
    lea rsi,[rel emsg]
    call puts
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
    jmp .err
.usage:
    lea rsi,[rel umsg]
    call puts
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
    jmp .usage

puts:
    mov rax, SYS_WRITE
    mov rdi, 1
    xor rdx, rdx
.len:
    cmp byte [rsi+rdx], 0
    je .sy
    inc rdx
    jmp .len
.sy:
    push rcx
    int 0x80
    pop rcx
    ret

section .rodata
nl: db 10,0
emsg: db "cat: nao encontrado ou sem permissao\n",0
umsg: db "cat: uso: cat <arquivo>\n",0