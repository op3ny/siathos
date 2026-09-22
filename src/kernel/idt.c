#include "thais.h"
#include <stdbool.h>

/* idt.c — IDT x86_64. Monta as gates para os 48 vectors (32 excecoes + 16 IRQ)
   e instala os stubs externos isrN. As gates sao de anel 0 (0x8E). */

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void isr32(void); extern void isr33(void); extern void isr34(void);
extern void isr35(void); extern void isr36(void); extern void isr37(void);
extern void isr38(void); extern void isr39(void); extern void isr40(void);
extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void);
extern void isr47(void);
extern void syscall_gate(void);   /* int 0x80, DPL=3 (user mode) */

static idt_entry_t idt[256];
idtr_t idtp;
static bool idt_ready=false;

static void set_idt_gate(int n, uint64_t base, uint8_t attr){
    idt[n].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[n].offset_mid  = (uint16_t)((base>>16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((base>>32) & 0xFFFFFFFF);
    idt[n].selector    = 0x08;   /* kernel code segment */
    idt[n].ist         = 0;
    idt[n].type_attr   = attr;
    idt[n].zero        = 0;
}

void idt_init(void){
    /* limpa */
    for(int i=0;i<256;i++){ idt[i].offset_low=0; idt[i].selector=0; idt[i].ist=0; idt[i].type_attr=0; idt[i].offset_mid=0; idt[i].offset_high=0; idt[i].zero=0; }

    void *handlers[48] = {
        (void*)isr0,(void*)isr1,(void*)isr2,(void*)isr3,(void*)isr4,(void*)isr5,(void*)isr6,(void*)isr7,
        (void*)isr8,(void*)isr9,(void*)isr10,(void*)isr11,(void*)isr12,(void*)isr13,(void*)isr14,(void*)isr15,
        (void*)isr16,(void*)isr17,(void*)isr18,(void*)isr19,(void*)isr20,(void*)isr21,(void*)isr22,(void*)isr23,
        (void*)isr24,(void*)isr25,(void*)isr26,(void*)isr27,(void*)isr28,(void*)isr29,(void*)isr30,(void*)isr31,
        (void*)isr32,(void*)isr33,(void*)isr34,(void*)isr35,(void*)isr36,(void*)isr37,(void*)isr38,(void*)isr39,
        (void*)isr40,(void*)isr41,(void*)isr42,(void*)isr43,(void*)isr44,(void*)isr45,(void*)isr46,(void*)isr47
    };
    for(int i=0;i<48;i++) set_idt_gate(i,(uint64_t)handlers[i],0x8E);

    /* gate de syscall para user mode: vector 0x80, DPL=3 (0xEE) */
    set_idt_gate(0x80,(uint64_t)syscall_gate,0xEE);

    idtp.base=(uint64_t)&idt[0];
    idtp.limit=(uint16_t)(sizeof(idt_entry_t)*256 - 1);
    idt_ready=true;
    kprint("[arkhe] IDT montada\n");
}

void idt_load(void){ if(idt_ready) load_idt(); }

void pic_remap(void){
    outb_port(0x20,0x11); outb_port(0xA0,0x11);
    outb_port(0x21,0x20); outb_port(0xA1,0x28);
    outb_port(0x21,0x04); outb_port(0xA1,0x02);
    outb_port(0x21,0x01); outb_port(0xA1,0x01);
    outb_port(0x21,0xFE); outb_port(0xA1,0xFF);   /* master: desmascara IRQ0 (timer) */
}

void pic_eoi(uint8_t irq){
    if(irq>=8) outb_port(0xA0,0x20);
    outb_port(0x20,0x20);
}

void enable_interrupts(void){ __asm__ volatile("sti"); }
void disable_interrupts(void){ __asm__ volatile("cli"); }
