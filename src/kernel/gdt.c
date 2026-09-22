#include "thais.h"
#include <stdbool.h>

/* gdt.c — GDT + TSS (Phase K). Prepara segmentos de kernel (0x08/0x10) e de
   usuário (0x18/0x20), além de um TSS para o switch de stack anel3->anel0. */

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

/* TSS (64-bit) */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1,ist2,ist3,ist4,ist5,ist6,ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

static gdt_entry_t gdt[7];
static tss_t tss;
static gdtr_t gdtr;

static void set_gdt_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags){
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base>>16) & 0xFF;
    gdt[i].access    = access;
    gdt[i].flags     = (uint8_t)(((limit>>16)&0x0F) | (flags & 0xF0));
    gdt[i].base_high = (base>>24) & 0xFF;
}

void gdt_init(void){
    /* null */
    set_gdt_entry(0,0,0,0,0);
    /* kernel code 0x08 */
    set_gdt_entry(1,0,0xFFFFF,0x9A,0xA0);
    /* kernel data 0x10 */
    set_gdt_entry(2,0,0xFFFFF,0x92,0xA0);
    /* user code 0x18 */
    set_gdt_entry(3,0,0xFFFFF,0xFA,0xA0);
    /* user data 0x20 */
    set_gdt_entry(4,0,0xFFFFF,0xF2,0xA0);

    /* TSS 0x28 */
    memset(&tss,0,sizeof(tss));
    tss.iomap_base=sizeof(tss);
    uint64_t tss_base=(uint64_t)&tss;
    uint32_t limit=(uint32_t)(sizeof(tss)-1);
    /* segment descriptor do TSS (acesso 0x89, gran 0) */
    gdt[5].limit_low=limit&0xFFFF;
    gdt[5].base_low=(tss_base)&0xFFFF;
    gdt[5].base_mid=(tss_base>>16)&0xFF;
    gdt[5].access=0x89;
    gdt[5].flags=(uint8_t)(((limit>>16)&0x0F)&0x0F);
    gdt[5].base_high=(tss_base>>24)&0xFF;
    /* second half of TSS descriptor (upper 32 bits of base) */
    uint32_t base_hi=(uint32_t)(tss_base>>32);
    gdt[6].limit_low=(uint16_t)base_hi;
    gdt[6].base_low=(uint16_t)(base_hi>>16);
    gdt[6].base_mid=(uint8_t)(base_hi>>24);
    gdt[6].access=0;
    gdt[6].flags=0;
    gdt[6].base_high=0;

    gdtr.limit=(uint16_t)(sizeof(gdt)-1);
    gdtr.base=(uint64_t)&gdt[0];

    load_gdtr();

    /* carrega TR com o seletor do TSS (0x28 = indice 5, RPL 0) */
    __asm__ volatile("ltr %%ax"::"a"((uint16_t)0x28));
    kprint("[arkhe] GDT/TSS prontos\n");
}

/* atualiza o RSP do kernel usado ao entrar de anel 3 para anel 0 */
void gdt_set_kernel_stack(uint64_t rsp){
    tss.rsp0=rsp;
}

void load_gdtr(void){
    __asm__ volatile("lgdt %0"::"m"(gdtr));
    /* Reinicia CS/SS/DS/ES para os seletores do KERNEL (0x08/0x10). SEM isso o
       kernel roda com o CS "sujo" herdado do bootloader (ex.: 0x38), que e
       invalido no novo GDT -> qualquer iretq (timer/syscall/ring3) dah #GP
       (err=seletor), pois iretq re-valida CS. */
    __asm__ volatile(
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        "pushq $0x08\n\t"          /* CS de destino (kernel code) */
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "retfq\n\t"
        "1:\n\t"
        ::: "rax", "memory");
}
