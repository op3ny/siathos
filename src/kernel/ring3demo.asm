; ring3demo.asm - processo de demonstracao em RING 3
; Programinha freestanding de usuario: escreve "RING3-OK" no serial (SYS_WRITE)
; e entao fica em loop cedendo a CPU (SYS_YIELD). Se apreccao/retorno ring3
; funcionarem, o scheduler alterna com os demais processos sem fault.
; Base virtual: carregado em 0x400000; codigo 100% RIP-relative (PIC).
bits 64
section .text
global _start
_start:
    mov rax, 1            ; SYS_WRITE
    mov rdi, 1            ; fd=1 (serial)
    lea rsi, [rel msg]    ; mensagem (RIP-relative)
    mov rdx, mlen         ; tamanho
    int 0x80
.loop:
    mov rax, 6            ; SYS_YIELD
    int 0x80
    jmp .loop
msg: db "RING3-OK",0
mlen equ $ - msg
