; Siath OS - suporte a user mode (ring 3) x86_64
; enter_user : transicao ring0 -> ring3 via iretq (frame de usuario no stack)
; syscall_gate: handler do int 0x80 (ring3 -> ring0, retorna via iretq)
bits 64
section .text

extern syscall_dispatch

; void enter_user(void)
; Entra em ring-3 fazendo iretq com o frame de usuario que esta no topo do
; stack do kernel do processo (colocado por proc_create_user).
; Frame (RSP aponta para o SS, ordem de passagem do iretq):
;   [rsp+ 0] = SS (0x20, user data)
;   [rsp+ 8] = user RSP
;   [rsp+16] = RFLAGS
;   [rsp+24] = CS (0x18, user code)
;   [rsp+32] = user RIP
; O chamador DEVE ter setado tss.rsp0 para o topo deste kernel stack antes,
; para que qualquer int/IRQ vindo de ring3 volte para ANEL 0 num stack valido.
global enter_user
enter_user:
    ; argc/argv para o _start do app em ring 3, colocados por proc_create_user_args
    ; dois slots abaixo do frame iretq: [rsp-24]=argc, [rsp-16]=argv_va (rsp=ktop).
    ; rdi=argc, rsi=argv_va (0/0 quando o app nao usa args).
    mov rdi, [rsp-24]
    mov rsi, [rsp-16]
    iretq

; void user_trampoline(void)
; Ponte para quebrar o modelo 'ret' do context_switch_first: o entry guardado
; em [ctx.rsp] e este endereco; o 'ret' chega aqui e pulamos para enter_user
; com rsp ja apontando para o frame iretq colocado logo acima (em ctx.rsp+8).
global user_trampoline
user_trampoline:
    jmp enter_user

; void syscall_gate(void)
; Invocado por 'int 0x80' de um processo ring-3. A CPU usou tss.rsp0 (kernel
; stack), salvou SS,userRSP,RFLAGS,CS,userRIP e mudou para CS=0x08 (ring0).
; Convencao de usuario:
;   rax = numero do syscall
;   rdi, rsi, rdx, rcx, r8 = args
;   retorno em rax
; syscall_dispatch(nr, a1, a2, a3, a4, a5)
; IMPORTANTE (ABI 1.0, MARCO 1): o gate preserva TODOS os GPRs do app, voltando
; intactos ao ring 3 — só RAX contem o resultado. Sem isso, o kernel (codigo C
; livre para usar registradores caller-saved) zerava argv guardado em r9 entre
; duas chamadas (hello.main pagava PF com argv=0xffffff apos o 1o SYS_WRITE).
global syscall_gate
syscall_gate:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    ; rsp+0x00=rax(nr) 0x08=rbx 0x10=rcx 0x18=rdx 0x20=rsi 0x28=rdi
    ; 0x30=rbp 0x38=r8 0x40=r9 0x48=r10 0x50=r11 0x58=r12 0x60=r13
    ; 0x68=r14 0x70=r15
    mov rdi, [rsp + 0x00]    ; nr
    mov rsi, [rsp + 0x28]    ; a1 (rdi)
    mov rdx, [rsp + 0x20]    ; a2 (rsi)
    mov rcx, [rsp + 0x18]    ; a3 (rdx)
    mov r8,  [rsp + 0x10]    ; a4 (rcx)
    mov r9,  [rsp + 0x38]    ; a5 (r8)
    call syscall_dispatch
    mov [rsp + 0x00], rax    ; guarda o resultado no slot do rax salvo
    pop rax                  ; rax = resultado
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    iretq               ; volta a ring3 no ponto de origem

section .note.GNU-stack noalloc noexec nowrite progbits
