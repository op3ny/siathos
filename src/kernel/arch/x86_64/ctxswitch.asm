; Siaht OS - context switch x86_64 (cooperative + first-start + resume por frame IRQ)
; Made with love by Thais (op3n/op3ny)
;
; cpu_context_t layout (8-byte slots), matching thais.h:
;  0  r15   1 r14   2 r13   3 r12   4 r11   5 r10   6 r9   7 r8
;  8  rbp   9 rbx   10 rip   11 rsp
;  12 cr3   13 arg1   14 arg2   15 ran   16 style(u8)   17 hw_rsp
; Offset de ctx dentro de process_t = 96 (0x60).
;    process_t: rib=0xB0 rsp=0xB8 cr3=0xC0 arg1=0xC8 arg2=0xD0 ran=0xD8
;               style=0xE0 hw_rsp=0xE8
;    cpu_context_t (context_switch usa cpu_context_t*): +0x50/0x58/0x60/0x68/
;               0x70/0x78/0x80(style)/0x88(hw_rsp)
;
; Duas CLASSES de retomada:
;   style=0 (C): processo parkou voluntariamente (sched_yield/switch). ctx.rsp
;                aponta para o return address dentro da cadeia C; resume via
;                'mov rsp, rsp; ret' (USADO na 1a execucao tambem).
;   style=1 (IRQ): processo foi preemptado por ISR. ctx.hw_rsp = base do bloco
;                de GPRs deixado pelo stub + frame da CPU no kernel stack.
;                Resume via 'pop GPRs; add rsp,16; iretq' — nunca re-entra em
;                cadeias C (fix do stall pos-exit).
bits 64
section .text

; void context_switch(cpu_context_t *from, cpu_context_t *to)
; Salva o contexto do processo atual (callee-saved + rsp/rip) em *from e
; restaura a partir de *to. Tambem troca o espaco de enderecos (CR3).
; Retorna (via cadeia C ou via iretq) quando o processo for re-agendado.
global context_switch
context_switch:
    ; salva callee-saved registers no struct de origem
    mov [rdi + 0*8], r15
    mov [rdi + 1*8], r14
    mov [rdi + 2*8], r13
    mov [rdi + 3*8], r12
    mov [rdi + 4*8], r11
    mov [rdi + 5*8], r10
    mov [rdi + 6*8], r9
    mov [rdi + 7*8], r8
    mov [rdi + 8*8], rbp
    mov [rdi + 9*8], rbx
    ; park C: retomada por ret (salva return address). marca style=0.
    mov byte [rdi + 0x80], 0
    mov [rdi + 11*8], rsp
    mov rax, [rsp]
    mov [rdi + 10*8], rax            ; rip = endereco de retorno (usa-se em debug)
    ; salva o espaco de enderecos atual
    mov rax, cr3
    mov [rdi + 12*8], rax

    ; restaura a partir do struct de destino
    mov r15, [rsi + 0*8]
    mov r14, [rsi + 1*8]
    mov r13, [rsi + 2*8]
    mov r12, [rsi + 3*8]
    mov r11, [rsi + 4*8]
    mov r10, [rsi + 5*8]
    mov r9,  [rsi + 6*8]
    mov r8,  [rsi + 7*8]
    mov rbp, [rsi + 8*8]
    mov rbx, [rsi + 9*8]
    ; troca para o espaco de enderecos do destino (flusha TLB; maps compartilham
    ; as tabelas de paginas do kernel, portanto tudo continua valido)
    mov rax, [rsi + 12*8]
    test rax, rax
    jz .no_cr3
    mov cr3, rax
.no_cr3:
    ; 1a execucao do destino? carrega rdi/rsi com argc/argv (slot 13/14) e
    ; marca ran (slot 15). rsp precisa ser carregado antes de clobberar rsi.
    mov rax, [rsi + 15*8]
    test rax, rax
    jnz .already_ran
    mov qword [rsi + 15*8], 1
    mov r8,  [rsi + 13*8]
    mov r9,  [rsi + 14*8]
    mov rsp, [rsi + 11*8]
    mov rdi, r8
    mov rsi, r9
    ret
.already_ran:
    cmp byte [rsi + 0x80], 1          ; park por IRQ (preempcao)?
    jne .al_c
    ; retomada por frame: iretq direto do bloco salvo pelo stub do ISR.
    ; cr3 do destino ja carregado acima (.no_cr3). hw_rsp = base do bloco.
    mov rsp, [rsi + 0x88]
    pop rdi
    pop rsi
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    pop rax                       ; rax interrompido (preservado pelo stub)
    add rsp, 16                       ; remove int_no + err -> frame da CPU
    iretq
.al_c:
    mov rsp, [rsi + 11*8]
    ret

; void context_switch_first(process_t *p)
; Bootstraps o primeiro processo: carrega o contexto inicial e "retorna" para
; o entry point colocado no topo do stack do processo. Se o processo ja havia
; sido preemptado (style=IRQ), retoma por frame (iretq) em vez da cadeia C.
; rdi = process_t*
global context_switch_first
context_switch_first:
    cmp byte [rdi + 0xE0], 1          ; retomada de preempcao (frame IRQ)?
    jne .first_c_entry
    mov rdx, [rdi + 0xC0]             ; cr3 do processo
    test rdx, rdx
    jz .fno_cr3
    mov cr3, rdx
.fno_cr3:
    mov rsp, [rdi + 0xE8]             ; hw_rsp = bloco GPRs + frame CPU
    pop rdi
    pop rsi
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    pop rax                       ; rax interrompido (preservado pelo stub)
    add rsp, 16
    iretq
.first_c_entry:
    mov byte [rdi + 0xE0], 0          ; entrada por ret (classe C)
    mov rax, [rdi + 0xB0]            ; ctx.rip = entry point
    mov r8,  [rdi + 0xC8]            ; ctx.arg1 (entry rdi): argc (1a execucao)
    mov r9,  [rdi + 0xD0]            ; ctx.arg2 (entry rsi): argv
    mov rsp, [rdi + 0xB8]            ; ctx.rsp = topo do stack (contem entry)
    mov qword [rdi + 0xD8], 1        ; ctx.ran = 1 (init ja executou)
    xor rbp, rbp
    xor rbx, rbx
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    ; carrega o espaco de enderecos do processo (PML4 proprio)
    mov rcx, [rdi + 0xC0]
    test rcx, rcx
    jz .no_cr3
    mov cr3, rcx
.no_cr3:
    mov rdi, r8                     ; argc -> rdi (convencao ABI)
    mov rsi, r9                     ; argv -> rsi
    ret                              ; ret "popa" o entry do stack e executa

section .note.GNU-stack noalloc noexec nowrite progbits
