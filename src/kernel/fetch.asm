; fetch.asm - app RING 3: imprime informacoes do sistema via SYS_SYSINFO.
; Entrada: nenhuma. PIC (RIP-relative), carregado por proc_create_user em um
; vaddr alto de usuario e executado em anel 3. Usa SYS_SYSINFO (12).
bits 64
section .text
global _start

SYS_WRITE   equ 1
SYS_EXIT    equ 0
SYS_SYSINFO equ 12
MIB         equ 1048576

; Layout system_info_t (ordem de sysinfo.c / thais.h):
;   [rsp+ 0] memory_total (u64)
;   [rsp+ 8] memory_free  (u64)
;   [rsp+16] memory_used  (u64)
;   [rsp+24] cpu_count    (u32)
;   [rsp+28] process_count(u32)
;   [rsp+32] uptime_ms    (u64)
;   [rsp+40] ticks        (u64)

_start:
    sub rsp, 128                  ; system_info_t + espaco p/ itoa
    mov rax, SYS_SYSINFO
    mov rdi, rsp                  ; a1 = &info
    int 0x80

    lea rsi, [rel t_header]
    call puts
    lea rsi, [rel newl]
    call puts

    ; memoria total [MiB]
    lea rsi, [rel s_total]
    call puts
    mov rsi, [rsp+0]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    ; memoria usada [MiB]
    lea rsi, [rel s_used]
    call puts
    mov rsi, [rsp+16]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    ; memoria livre [MiB]
    lea rsi, [rel s_free]
    call puts
    mov rsi, [rsp+8]
    call putmib
    lea rsi, [rel s_mib]
    call puts
    lea rsi, [rel newl]
    call puts

    ; processos ativos
    lea rsi, [rel s_proc]
    call puts
    mov eax, [rsp+28]             ; process_count (u32)
    mov rsi, rax
    call putu64
    lea rsi, [rel s_procx]
    call puts
    lea rsi, [rel newl]
    call puts

    ; uptime (ms -> s)
    lea rsi, [rel s_up]
    call puts
    mov rax, [rsp+32]             ; uptime_ms
    xor edx, edx
    mov ecx, 1000
    div rcx                       ; rax = segundos
    mov rsi, rax
    call putu64
    lea rsi, [rel s_sec]
    call puts
    lea rsi, [rel newl]
    call puts

    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80
.done:
    jmp .done

; putmib(u64 rsi) - imprime rsi / 1MiB em decimal.
putmib:
    mov rax, rsi
    xor edx, edx
    mov rcx, MIB
    div rcx                       ; rax = rsi/1MiB
    mov rsi, rax
    jmp putu64

; puts(const char *s) - escreve string NUL de rsi via SYS_WRITE.
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

; putu64(u64 rsi) - imprime rsi em decimal. Clobbera rax,rcx,rdx,rdi,rsi,r8.
putu64:
    mov rax, rsi
    lea r8, [rsp + 96]            ; buffer itoa na area reservada (rsp..rsp+128)
    mov byte [r8], 0
    dec r8
    mov ecx, 10
.next:
    xor edx, edx
    div rcx                       ; rax/=10, rdx=digito
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
t_header:
    db "Siath OS",0
s_total:
    db "Memoria total : ",0
s_used:
    db "Memoria usada : ",0
s_free:
    db "Memoria livre : ",0
s_proc:
    db "Processos     : ",0
s_procx:
    db " ativos",0
s_up:
    db "Uptime        : ",0
s_sec:
    db " s",0
s_mib:
    db " MiB",0
newl:
    db 10,0
