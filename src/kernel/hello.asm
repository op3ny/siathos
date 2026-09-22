; hello.asm — hello ELF demo (Fase O: argc/argv via exec_elf_args).
; Entrada: rdi = argc, rsi = argv (ctx.arg1/arg2 -> context_switch_first).
; Imprime "[hello]" + cada argumento (argc/argv reais) via SYS_WRITE e sai
; com SYS_EXIT. Ring 0 nesta fase (proc_create_argv); int 0x80 funciona de
; ring 0 pois syscall_gate faz iretq para o mesmo CPL.
; Linkado em 0x4000000 (64MB): o ELF loader (elf.c) so carrega segmentos em
; faixas que o PMM ainda nao entregou (audit Phase A).
bits 64
section .text
global _start

_start:
    mov r12, rdi                  ; argc (r12-r15: callee-saved, preservados)
    mov r13, rsi                  ; argv
    lea rsi, [rel hdr]
    call puts
    xor r15, r15                  ; indice do argumento
.argloop:
    cmp r15, r12
    jge .done
    mov rsi, [r13 + r15*8]        ; argv[r15]
    test rsi, rsi
    jz .done
    call puts
    lea rsi, [rel spc]
    call puts
    inc r15
    jmp .argloop
.done:
    lea rsi, [rel nl]
    call puts
    mov rax, 0                    ; SYS_EXIT (ring0 -> proc_exit)
    mov rdi, 0                    ; exit code 0
    int 0x80
.done2:
    jmp .done2                    ; nunca alcancado (proc_exit nao retorna)

; puts(const char *s) — escreve s no console+serial via SYS_WRITE.
; Clobbera rax, rdi, rsi, rdx, rcx; preserva os demais.
puts:
    mov rax, 1                    ; SYS_WRITE
    mov rdi, 1                    ; fd (ignorado)
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

section .rodata
hdr: db "[hello] args:",0
spc: db " ",0
nl:  db 10,0

section .note.GNU-stack noalloc noexec nowrite progbits