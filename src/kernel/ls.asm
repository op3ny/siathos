; ls.asm - app RING 3: lista um diretorio via SYS_FS_LIST.
; Entrada: rdi=argc, rsi=argv_va. dir = argv[1] ou "/".
; Buffer na STACK (mapeada RW). PIC (RIP-relative).
bits 64
section .text
global _start
SYS_FS_LIST equ 9
SYS_WRITE   equ 1
SYS_EXIT    equ 0

_start:
    mov r12, rsi            ; argv_va
    mov r13, rdi            ; argc
    lea r14, [rel root]     ; dir padrao "/"
    cmp r13, 2
    jl .have_dir
    mov rax, r12
    mov r14, [rax + 8]      ; argv[1]
.have_dir:
    sub rsp, 4096           ; buffer na stack
    mov r15, rsp
    mov rax, SYS_FS_LIST
    mov rdi, r14            ; a1 = dir
    mov rsi, r15            ; a2 = buf
    mov rdx, 4096           ; a3 = tamanho
    int 0x80
    test rax, rax
    jnz .err
    mov rsi, r15            ; imprime a listagem (string NUL)
    call puts
.done:
    add rsp, 4096
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
root: db "/",0
emsg: db "ls: falhou (caminho invalido ou sem permissao)\n",0