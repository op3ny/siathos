#include "thais.h"
#include <stdbool.h>

/* paging.c — Memoria virtual (Phase P).
   Cada processo recebe um espaco de enderecos proprio (PML4 unico) criado por
   paging_new_address_space(), que e um clone "raso" das tabelas de paginas do
   kernel: aponta para as mesmas PDPT/PD/PT (todas as entradas do kernel sao
   compartilhadas), de modo que nada deixa de resolver sob nenhum CR3. Isso
   estabelece o isolamento de topo (PML4 por processo) e o hook para troca de
   CR3 no context switch, sem quebrar o modelo identidade+higher-half atual.

   AUDITORIA (seguranca):
   - Habilitamos NXE (EFER.NXE) para que o bit de nao-execucao (bit 63) possa
     marcar paginas de usuario como NAO executaveis (W^X real).
   - Paginas de CODIGO de usuario sao mapeadas P|US|X  (0x5): executa, nao grava.
     Paginas de STACK de usuario sao mapeadas P|US|W|NX (0x7|NX): grava, nao executa.
   - paging_free_address_space() desfaz um PML4 de processo quando ele morre,
     liberando SOMENTE o que o processo criou (nunca tabelas compartilhadas do
     kernel — detectadas por comparacao com o PML4 atual/kernel). */

static uint64_t saved_cr3=0;

static uint64_t read_cr3(void){
    uint64_t v=0; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v;
}
static void write_cr3(uint64_t v){ __asm__ volatile("mov %0,%%cr3"::"r"(v):"memory"); }

static uint64_t rdmsr_64(uint32_t msr){
    uint32_t lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi<<32)|lo;
}
static void wrmsr_64(uint32_t msr, uint64_t v){
    __asm__ volatile("wrmsr"::"a"((uint32_t)v),"d"((uint32_t)(v>>32)),"c"(msr):"memory");
}

#define MSR_EFER 0xC0000080ULL
#define EFER_NXE (1ULL<<11)

void paging_init(void){
    saved_cr3=read_cr3();
    /* habilita NXE: o bit 63 das PTEs (=NX) passa a ter efeito. O kernel
       mapa identidade/higher-half nao seta NX, portanto continua executavel;
       apenas paginas de usuario marcadas com bit 63 (stack) ficam NAO-X. */
    wrmsr_64(MSR_EFER, rdmsr_64(MSR_EFER) | EFER_NXE);
    kprint("[arkhe] memoria virtual: identidade+higher-half compartilhado (NXE on, W^X)\n");
}

/* Converte um endereco virtual baixo para o índice de cada nível de tabela.
   (usuario vive em user-space < 0x00008000_00000000; kernel em higher-half) */
static inline uint64_t idx_l4(uint64_t v){ return (v>>39)&0x1FF; }
static inline uint64_t idx_l3(uint64_t v){ return (v>>30)&0x1FF; }
static inline uint64_t idx_l2(uint64_t v){ return (v>>21)&0x1FF; }
static inline uint64_t idx_l1(uint64_t v){ return (v>>12)&0x1FF; }

#define PTE_ADDR 0x000FFFFFFFFFF000ULL

/* Cria um novo espaco de enderecos: aloca um PML4 fisico e o popula cloneando
   as 512 entradas do PML4 do kernel (clone raso -> compartilha as tabelas).
   Retorna o endereco fisico (valor de CR3) do novo espaco, ou 0 em falha. */
uint64_t paging_new_address_space(void){
    void *pml4v = pmm_alloc(1);
    if(!pml4v) return 0;
    uint64_t pml4 = (uint64_t)pml4v;
    uint64_t *dst = (uint64_t*)(uintptr_t)pml4;      /* identidade-mapped */
    uint64_t *src = (uint64_t*)(uintptr_t)saved_cr3;
    for(int i=0;i<512;i++) dst[i]=src[i];
    /* usuario começa sem nenhum mapeamento proprio (só o kernel compartilhado) */
    return pml4;
}

/* Cria (se necessario) a tabela do nivel indicado a partir do endereco da
   entrada pai. Retorna o endereco físico da tabela filha, ou 0 se a entrada
   pai for um mapeamento grande (2MB/1GB) que nao pode ser usado como tabela.
   'user' marca as tabelas intermediárias com o bit US (acessiveis de ring 3). */
static uint64_t paging_ensure_table(uint64_t *entry, bool user){
    if(*entry & 1){
        /* se for pagina grande (bit 7 = PS em L1/L2) ou entrada de L4/L3 com
           formato de pagina, recusamos para nao corromper o mapeamento */
        if(*entry & (1ULL<<7)) return 0;
        return *entry & PTE_ADDR;
    }
    uint64_t phys=(uint64_t)pmm_alloc(1);
    if(!phys) return 0;
    memset((void*)(uintptr_t)phys,0,4096);            /* identidade-mapped */
    uint64_t flags = 0x3;                             /* present | rw */
    flags |= user ? 0x4 : 0x0;                        /* user bit se pedido */
    *entry = (phys & PTE_ADDR) | flags;
    return phys;
}

/* Mapeia uma pagina de 4KB (virt -> phys) no espaco de enderecos 'pml4'.
   'flags' sao as flags de nivel de pagina (bit layout x86-64). O chamador
   decide W^X: codigo -> 0x5 (P|US), stack/dados -> 0x7|(1ULL<<63) (P|US|W|NX).
   Retorna 0 em falha, !=0 em sucesso. */
int paging_map_user(uint64_t pml4, uint32_t dst_pid, uint64_t virt, uint64_t phys, uint64_t flags){
    (void)dst_pid;
    if(!pml4 || (virt & 0xFFF) || (phys & 0xFFF)) return 0;
    uint64_t *l4=(uint64_t*)(uintptr_t)pml4;

    uint64_t l4i=idx_l4(virt);
    uint64_t *l3=(uint64_t*)(uintptr_t)paging_ensure_table(&l4[l4i], true);
    if(!l3) return 0;

    uint64_t l3i=idx_l3(virt);
    uint64_t *l2=(uint64_t*)(uintptr_t)paging_ensure_table(&l3[l3i], true);
    if(!l2) return 0;

    uint64_t l2i=idx_l2(virt);
    uint64_t *l1=(uint64_t*)(uintptr_t)paging_ensure_table(&l2[l2i], true);
    if(!l1) return 0;

    uint64_t l1i=idx_l1(virt);
    l1[l1i] = (phys & PTE_ADDR) | flags;
    __asm__ volatile("invlpg (%0)"::"r"(virt):"memory");
    return 1;
}

/* Desmapeia uma pagina de 4KB (limpa a folha) no espaco 'pml4'. Nao libera a
   pagina fisica; devolve o frame que estava mapeado (0 se nao havia). Usado
   pelo rollback do SYS_MMAP (e munmap futuro). */
uint64_t paging_unmap_user(uint64_t pml4, uint64_t virt){
    if(!pml4 || (virt & 0xFFF)) return 0;
    uint64_t *l4=(uint64_t*)(uintptr_t)pml4;
    uint64_t e4=l4[idx_l4(virt)];
    if(!(e4&1)) return 0;
    uint64_t *l3=(uint64_t*)(uintptr_t)(e4&PTE_ADDR);
    uint64_t e3=l3[idx_l3(virt)];
    if(!(e3&1)) return 0;
    uint64_t *l2=(uint64_t*)(uintptr_t)(e3&PTE_ADDR);
    uint64_t e2=l2[idx_l2(virt)];
    if(!(e2&1)) return 0;
    if(e2 & (1ULL<<7)) return 0;                     /* pagina enorme: nao tocamos */
    uint64_t *l1=(uint64_t*)(uintptr_t)(e2&PTE_ADDR);
    uint64_t e1=l1[idx_l1(virt)];
    if(!(e1&1)) return 0;
    l1[idx_l1(virt)]=0;
    __asm__ volatile("invlpg (%0)"::"r"(virt):"memory");
    return e1 & PTE_ADDR;
}

/* Endereco fisico de uma VA de usuario (para DMA do driver: XHCI precisa do
   PA dos aneis/contextos). Caminha as tabelas do PML4 do processo; 0 se nao
   mapeada. So aceita user space (< 0x800000000000). */
uint64_t paging_phys_of_user(uint64_t pml4, uint64_t virt, int *is_mmio){
    if(!pml4 || virt > 0x00007fffffffffffULL) return 0;
    uint64_t *l4=(uint64_t*)(uintptr_t)pml4;
    uint64_t e4=l4[idx_l4(virt)];
    if(!(e4&1)) return 0;
    uint64_t *l3=(uint64_t*)(uintptr_t)(e4&PTE_ADDR);
    uint64_t e3=l3[idx_l3(virt)];
    if(!(e3&1)) return 0;
    uint64_t *l2=(uint64_t*)(uintptr_t)(e3&PTE_ADDR);
    uint64_t e2=l2[idx_l2(virt)];
    if(!(e2&1)) return 0;
    if(e2 & (1ULL<<7)) return 0;                     /* pagina grande: sem PTE folha */
    uint64_t *l1=(uint64_t*)(uintptr_t)(e2&PTE_ADDR);
    uint64_t e1=l1[idx_l1(virt)];
    if(!(e1&1)) return 0;
    if(is_mmio) *is_mmio = (e1 & PTE_AVAIL_MMIO) ? 1 : 0;
    return (e1 & PTE_ADDR) + (virt & 0xFFF);
}

/* ---- Teardown de espaco de enderecos (Auditoria: W^X/GC) ----
   Libera apenas o que o processo CRIOU. Entradas do PML4 que sao idênticas
   as do PML4 do kernel/saved_cr3 sao compartilhadas e NAO sao tocadas.
   Entradas presentes diferentes indicam arvores alocadas pelo processo
   (user space); libera recursivamente as paginas folha e as tabelas. */
static void paging_free_subtree(uint64_t table, int level){
    uint64_t *e=(uint64_t*)(uintptr_t)table;
    for(int i=0;i<512;i++){
        uint64_t ent=e[i];
        if(!(ent&1)) continue;
        if(ent & (1ULL<<7)){
            /* pagina grande (2MB/1GB) criada pelo processo... nao deveria
               existir (mapeamos apenas 4KB); por seguranca nao a libera. */
            continue;
        }
        uint64_t child=ent & PTE_ADDR;
        if(level>1)
            paging_free_subtree(child, level-1);
        else if(!(ent & PTE_AVAIL_MMIO))
            pmm_free((void*)(uintptr_t)child, 1);   /* pula frames MMIO (device BAR) */
    }
    pmm_free((void*)(uintptr_t)table, 1);
}

void paging_free_address_space(uint64_t pml4){
    if(!pml4 || pml4==saved_cr3) return;      /* nunca desfaz o espaco do kernel */
    uint64_t *l4=(uint64_t*)(uintptr_t)pml4;
    uint64_t *ker=(uint64_t*)(uintptr_t)saved_cr3;
    for(int i=0;i<512;i++){
        uint64_t ent=l4[i];
        if(!(ent&1)) continue;
        if(ent==ker[i]) continue;             /* compartilhada com o kernel */
        paging_free_subtree(ent & PTE_ADDR, 3);
    }
    pmm_free((void*)(uintptr_t)pml4, 1);
}

void paging_load(uint64_t pml4){
    write_cr3(pml4);
}

/* Mapeia no espaco do processo 'pml4' (PML4 existente) uma regiao de MMIO em
   identidade (virt==phys) com paginas de 2MB (P|RW|PS). Cria as tabelas L3/L2
   intermediarias se necessario. */
static void pml4_map_device(const uint64_t pml4, uint64_t phys){
    uint64_t *l4=(uint64_t*)(uintptr_t)pml4;
    uint64_t e=idx_l4(phys);
    if(!(l4[e]&1)){
        uint64_t tab=(uint64_t)pmm_alloc(1);
        if(!tab) return;
        memset((void*)(uintptr_t)tab,0,4096);
        l4[e]=(tab&PTE_ADDR)|0x3;
    }
    uint64_t *l3=(uint64_t*)(uintptr_t)(l4[e]&PTE_ADDR);
    uint64_t e3=idx_l3(phys);
    if(!(l3[e3]&1)){
        uint64_t tab=(uint64_t)pmm_alloc(1);
        if(!tab) return;
        memset((void*)(uintptr_t)tab,0,4096);
        l3[e3]=(tab&PTE_ADDR)|0x3;
    }
    uint64_t *l2=(uint64_t*)(uintptr_t)(l3[e3]&PTE_ADDR);
    l2[idx_l2(phys)]=(phys&PTE_ADDR)|0x3ULL|(1ULL<<7);
}

/* Mapeia MMIO em identidade no espaco do kernel (saved_cr3) e em todos os
   PML4 de processos ja criados (clones rasos: os gerados depois herdam do
   kernel via paging_new_address_space). Usado p/ BARs acima de 4GB (ex.
   xHCI 64-bit em QEMU, ex. 0xc000000000), que nao estao no mapa inicial. */
void paging_map_device_identity(uint64_t phys, uint64_t bytes){
    uint64_t start=phys&~0x1FFFFFULL;
    uint64_t end=((phys+bytes+0x1FFFFFULL)&~0x1FFFFFULL);
    for(uint64_t a=start;a<end;a+=0x200000ULL){
        pml4_map_device(saved_cr3,a);
        for(int i=0;i<MAX_PROCS;i++){
            process_t *p=proc_at(i);
            if(!p||!p->present||!p->ctx.cr3||p->ctx.cr3==saved_cr3) continue;
            pml4_map_device(p->ctx.cr3,a);
        }
    }
    char b[128];
    snprintf(b,128,"[arkhe] mmio identidade 0x%llx +%llu bytes\n",
             (unsigned long long)start,(unsigned long long)bytes);
    kprint(b);
}

/* retorna o CR3 ativo (usado pelo GC de zombies para nunca liberar o espaco
   de enderecos do processo que esta rodando agora) */
uint64_t paging_current_cr3(void){
    return read_cr3();
}