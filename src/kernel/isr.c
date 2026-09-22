#include "thais.h"
#include <stdbool.h>

/* isr.c — desvio de interrupcoes excecoes (fault handlers) e IRQs.
   O asm (idt.asm) salva os GPRs e chama irq_common_handler(int_no, err). */

volatile uint64_t ticks=0;

static void (*irq_handlers[16])(void)={0};

void irq_install_handler(int irq, void (*handler)(void)){
    if(irq>=0 && irq<16) irq_handlers[irq]=handler;
}

void pit_ack(void){ }

/* chamado PELO STUB (idt.asm) no momento do park por preempcao, com a base
   do bloco de GPRs ja conhecida (rsp). Valida que essa base fica DENTRO da
   kstack do processo de origem (g_isr_from) — corrupcao de ctx aparece aqui
   na origem, antes do panic aleatorio la na frente. */
void isr_park_check(uint64_t base){
    if(!g_isr_from || !g_isr_from->stack) return;
    uint64_t lo=(uint64_t)(uintptr_t)g_isr_from->stack;
    uint64_t hi=lo+PROC_STACK_SIZE;
    if(base<lo || base>hi){
        char pb[160]; snprintf(pb,160,"[irq] ATENCAO: park hw=0x%llx FORA da kstack %s (0x%llx..0x%llx)\n",
            (unsigned long long)base, g_isr_from->name,
            (unsigned long long)lo, (unsigned long long)hi);
        kprint(pb);
    }
}

static const char *exception_names[32]={
    "Divisao por zero","Debug","NMI","Breakpoint","Overflow","BOUND","Opcode invalido",
    "No FPU","Double Fault","Reservado (9)","TSS Invalida","Segmento ausente",
    "Stack Fault","General Protection Fault","Page Fault","Reservado (15)",
    "x87 FP","Alinhamento","Machine Check","SIMD","Virtualizacao","Control Protection",
    "Reservado (22)","Reservado (23)","Reservado (24)","Reservado (25)",
    "Reservado (26)","Reservado (27)","Reservado (28)","Reservado (29)",
    "Security","Reservado (31)"
};

/* fault fatal de kernel (anel 0) */
extern uint64_t g_fault_rip;
extern uint64_t g_fault_cs;
extern uint64_t g_fault_fl;
extern uint64_t g_fault_rsp;
extern uint64_t g_fault_gprs;
static void kernel_panic_exception(uint64_t int_no, uint64_t err){
    /* PANIC visivel na tela mesmo durante o login (sem serial em notebook) */
    fb_console_panic();
    char b[220];
    snprintf(b,220,"\n[arkhe] EXCECAO %llu: %s (err=0x%llx) iretr_rip=0x%llx cs=0x%llx fl=0x%llx [rsp]=0x%llx\n", (unsigned long long)int_no,
        exception_names[int_no<32?int_no:0], (unsigned long long)err,
        (unsigned long long)g_fault_rip, (unsigned long long)g_fault_cs,
        (unsigned long long)g_fault_fl, (unsigned long long)g_fault_rsp);
    kprint(b);
    if(int_no==14){
        uint64_t cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
        snprintf(b,80,"[arkhe] PF CR2 (endereco): 0x%llx  (erro bit0=P, bit1=W, bit2=U)\n", (unsigned long long)cr2);
        kprint(b);
    }
    unsigned long spt; __asm__ volatile("mov %%rsp,%0":"=r"(spt));
    snprintf(b,80,"  rsp=0x%llx\n", (unsigned long long)spt);
    kprint(b);
    {
        /* o stub (idt.asm) salvou os 14 GPRs na ordem rdi,rsi,rdx,rcx,rbx,rbp,
           r8..r15 e anotou o ponteiro da area em g_fault_gprs. */
        unsigned long *q=(unsigned long*)g_fault_gprs;
        char d[640]; int p=0;
        static const char *nm[]={"rdi","rsi","rdx","rcx","rbx","rbp","r8","r9","r10","r11","r12","r13","r14","r15"};
        p+=snprintf(d+p,640-p,"  GPRs:");
        for(int i=0;i<14 && p<590;i++) p+=snprintf(d+p,640-p," %s=0x%llx",nm[i],(unsigned long long)q[i]);
        snprintf(d+p,640-p,"\n");
        kprint(d);
    }
    /* se houver processo atual, exibe contexto e limites do kernel stack */
    if(current_proc){
        snprintf(b,160,"  em processo '%s' pid %u\n", current_proc->name, current_proc->pid);
        kprint(b);
        if(current_proc->stack)
            snprintf(b,160,"  kstack 0x%llx..0x%llx (16KB) cur cr3=0x%llx\n",
                (unsigned long long)(uintptr_t)current_proc->stack,
                (unsigned long long)(uintptr_t)current_proc->stack+PROC_STACK_SIZE,
                (unsigned long long)current_proc->ctx.cr3);
        else
            snprintf(b,160,"  kstack=NULL (fora de processo?)\n");
        kprint(b);
    }
    kprint("[arkhe] PANIC - sistema parado\n");
    halt();
}

process_t *irq_common_handler(uint64_t int_no, uint64_t err){
    if(int_no < 32){
        /* excecao de anel 0 (kernel) -> fatal
           (page fault etc. de anel 3 seriam tratados por handler proprio) */
        kernel_panic_exception(int_no, err);
        return 0;
    }
    uint8_t irq=(uint8_t)(int_no-32);
    if(irq==0){ ticks++; }
    if(irq_handlers[irq]) irq_handlers[irq]();
    if(irq!=2) pic_eoi(irq);                     /* exceto slave cascade */
    /* preempcao (fix do stall): decide o alvo aqui, mas a TROCA em si acontece
       no stub (idt.asm), no nivel do frame IRQ — resume via iretq do alvo,
       nunca re-entrando em cadeias C de um park anterior. */
    if(irq==0 && current_proc && current_proc->state==PROC_RUNNING && sched_is_running()){
        process_t *t=sched_preempt_pick();
        return t;
    }
    return 0;
}
