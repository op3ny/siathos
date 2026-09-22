; Siaht OS - IDT / ISR / IRQ stubs x86_64
; Cada vector 0..47 tem um stub que empurra error code + numero, salva todos
; os GPRs e chama irq_common_handler(int_no, err). Exceptions sem error code
; recebem dummy 0. iretq ao final restaura a CPU.
bits 64
section .text

extern irq_common_handler
extern current_proc
extern g_isr_from
extern isr_park_check

%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0            ; dummy error code
    push %1           ; int number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1           ; int number (error code ja empilhado pela CPU)
    jmp isr_common_stub
%endmacro

; excecoes 0..31
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31
; IRQs 0..15 (remap 32..47)
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

section .data
align 8
global g_int_no
global g_err
global g_fault_rip
global g_fault_cs
global g_fault_fl
global g_fault_rsp
global g_fault_gprs
g_int_no: dq 0
g_err:    dq 0
g_fault_rip: dq 0
g_fault_cs: dq 0
g_fault_fl: dq 0
g_fault_rsp: dq 0
g_fault_gprs: dq 0

section .text
isr_common_stub:
    cli              ; garante IF=0 durante TODO o ISR (evita aninhamento)
    ; PRESERVA rax ANTES de qualquer uso: ISR assincrono nao tem fronteira de
    ; funcao; o handler C suja rax e o iretq voltava com rax lixo -> corrupcao
    ; em loops que usam rax como ponteiro (ex. fb_clear/lacos de pixels).
    push rax         ; [rsp]=saved_rax, [rsp+8]=int_no, [rsp+16]=err, ...
    lea rax, [rsp+8]
    mov [g_fault_rsp], rax
    mov rax, [rsp+8]
    mov [g_int_no], rax
    mov rax, [rsp+16]
    mov [g_err], rax
    mov rax, [rsp+24]
    mov [g_fault_rip], rax
    mov rax, [rsp+32]
    mov [g_fault_cs], rax
    mov rax, [rsp+40]
    mov [g_fault_fl], rax
    ; salva todos os GPRs (nao-volateis + argumento/scratch usados)
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rbx
    push rdx
    push rcx
    push rsi
    push rdi
    mov [g_fault_gprs], rsp       ; rdi em [rsp], rsi em [rsp+8], ... r15 em [rsp+104]
    mov rdi, [g_int_no]
    mov rsi, [g_err]
    call irq_common_handler      ; retorna process_t* (alvo) ou 0
    ; --- preempcao no nivel do frame: irq_common_handler devolve o alvo.
    ; O processo atual e estacionado com seu bloco de GPRs + frame da CPU
    ; (style=1) e o alvo e retomado via iretq — nunca re-entrando em cadeias C
    ; de um park anterior (fix do stall pos-exit). [rsp] aqui = base do bloco.
    test rax, rax
    jz .no_switch
    ; estaciona o processo QUE RODAVA quando o ISR chegou (g_isr_from).
    ; rax (alvo) preservado; rsp = base do bloco.
    mov rcx, [g_isr_from]        ; processo saindo
    test rcx, rcx
    jz .tgt_only
    mov [rcx + 0xE8], rsp          ; ctx.hw_rsp = base do bloco de GPRs
    mov byte [rcx + 0xE0], 1       ; ctx.style = 1 (resume por iretq)
    mov rdx, cr3
    mov [rcx + 0xC0], rdx          ; ctx.cr3 = cr3 atual (dono do ISR)
    mov rdi, rsp                   ; DIAG: base do park
    push rax                       ; preserva o alvo (rax e caller-saved no call)
    call isr_park_check            ; valida base dentro da kstack do FROM
    pop rax
.tgt_only:
    ; alvo ainda nao executou (ran==0)? -> primeira entrada (por ret)
    mov rdx, [rax + 0xD8]            ; ctx.ran
    test rdx, rdx
    jnz .resume_iret
    mov byte [rax + 0xE0], 0         ; style = 0 (entrada por ret)
    mov qword [rax + 0xD8], 1        ; ran = 1
    mov rcx, [rax + 0xC0]            ; cr3 do alvo
    test rcx, rcx
    jz .f1_nocr3
    mov cr3, rcx
.f1_nocr3:
    mov r8,  [rax + 0xC8]            ; argc
    mov r9,  [rax + 0xD0]            ; argv
    mov rsp, [rax + 0xB8]            ; ctx.rsp (contem entry)
    xor rbp, rbp
    xor rbx, rbx
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    mov rdi, r8
    mov rsi, r9
    ret                              ; ret "popa" o entry do stack e executa
.resume_iret:
    cmp byte [rax + 0xE0], 1          ; alvo parkado por IRQ (frame)?
    jne .resume_c                     ; se nao, park C -> resume por ret
    mov rdx, [rax + 0xC0]          ; cr3 do alvo
    test rdx, rdx
    jz .tno_cr3
    mov cr3, rdx
.tno_cr3:
    mov rsp, [rax + 0xE8]          ; hw_rsp do alvo
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
    pop rax                        ; restaura o rax interrompido
    add rsp, 16
    jmp .ireq
.resume_c:
    mov rdx, [rax + 0xC0]          ; cr3 do alvo (park C)
    test rdx, rdx
    jz .rc_no_cr3
    mov cr3, rdx
.rc_no_cr3:
    mov rsp, [rax + 0xB8]          ; ctx.rsp -> return address na cadeia C
    ret
.no_switch:
    ; restaura
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
    pop rax                        ; rax interrompido
    add rsp, 16        ; remove int_no + err; rsp -> frame iretq
    ; FIX (bugfix 2/3): o antigo bloco .diag_iret lia RIP/CS/RFLAGS/RSP do
    ; frame escrevendo em r10 DEPOIS dos pops e ANTES do iretq — corrompia o
    ; r10 do processo restaurado em TODO retorno de IRQ (preempcao e retorno
    ; simples). Processos ring-3 com estado vivo em r10d entre ticks (ex. o
    ; contador de indices do synd_st_seed) retomavam com r10 = bytes do frame
    ; (0x200000021f30) e o movslq %r10d,%rdi escrevia fora do store -> #PF
    ; de usuario (crash ~1/8, so quando o tick caia na janela). Os valores
    ; de diagnostico (g_fault_*) ja sao gravados na entrada do stub; aqui o
    ; iretq deve restaura TODOS os GPRs do usuario intactos.
.ireq:
    iretq

global load_idt
extern idtp
load_idt:
    lidt [idtp]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
