#include "thais.h"
#include <stdbool.h>

/* proc.c — processo real (PCB real) + scheduler cooperativo round-robin.
   Fases D, E, F. O contexto da CPU e guardado por context_switch().

   Estados: CREATED, READY, RUNNING, BLOCKED, TERMINATED. */

static process_t procs[MAX_PROCS];
static uint32_t next_pid=1;
static int current_index=0;
static bool scheduler_running=false;

process_t *current_proc=0;
/* processo que estava RODANDO quando o ISR decidiu preemptar (o stub o
   estaciona). sched_preempt_pick muda current_proc para o ALVO antes do stub
   agir; por isso o "from" e passado por aqui. */
process_t *g_isr_from=0;

void proc_init(void){
    for(int i=0;i<MAX_PROCS;i++){ procs[i].present=0; }
    next_pid=1; current_index=0; scheduler_running=false; current_proc=0;
    kprint("[kinesis] tabela de processos pronta\n");
}

static int find_free(void){
    for(int i=0;i<MAX_PROCS;i++) if(!procs[i].present || procs[i].state==PROC_TERMINATED) return i;
    return -1;
}

process_t* proc_get(uint32_t pid){
    for(int i=0;i<MAX_PROCS;i++) if(procs[i].present && procs[i].pid==pid) return &procs[i];
    return 0;
}
process_t* proc_at(int idx){
    if(idx<0||idx>=MAX_PROCS) return 0;
    return &procs[idx];
}

uint32_t proc_pid_of(process_t *p){ return p?p->pid:0; }

/* Entry-safety: se um processo foi criado sem entry (placeholder/registrado)
   e mesmo assim for agendado, aborta limpo em vez de executar endereco 0. */
static void proc_crash_dummy(void){
    kprint("[kinesis] placeholder sem entry agendado — abortando\n");
    proc_exit(1);
}

/* monta o contexto inicial do processo: stack proprio, entry no topo (16-alinhado)
   e espaço de enderecos (PML4 clone raso). arg1/arg2 sao argc/argv (Fase O);
   context_switch_first (ctxswitch.asm) os coloca em rdi/rsi na 1a execucao. */
static void proc_setup_stack(process_t *p, void (*entry)(void), int argc, const char **argv){
    p->ctx.rbp=0; p->ctx.rbx=0;
    p->ctx.r12=0; p->ctx.r13=0; p->ctx.r14=0; p->ctx.r15=0;
    p->ctx.r8=0; p->ctx.r9=0; p->ctx.r10=0; p->ctx.r11=0;
    p->ctx.arg1=0; p->ctx.arg2=0; p->ctx.ran=0;
    p->ctx.style=0; p->ctx.hw_rsp=0;

    /* stack proprio (kernel stack) */
    p->stack=kmalloc(PROC_STACK_SIZE);
    if(!p->stack){ p->present=0; return; }
    /* monta contexto inicial. IMPORTANTE (ABI x86-64):
       context_switch_first faz 'ret', que soma 8 ao RSP. Para a funcao de
       entrada ver RSP = 8 (mod 16), o ctx.rsp deve ser 16-alinhado.
       Garantimos um endereco 16-alinhado DENTRO da regiao de 16KB e gravamos
       o entry ali (nunca alem do fim do stack). */
    uintptr_t st = (uintptr_t)p->stack;
    st = (st + 15) & ~(uintptr_t)15;               /* base 16-alinhada */
    uint64_t top = (st + PROC_STACK_SIZE - 16);    /* 16-alinhado, dentro do bloco */
    top &= ~(uint64_t)15;
    *(uint64_t*)top = (uint64_t)entry;             /* ret -> entry */
    p->ctx.rsp = top;
    p->ctx.rip = (uint64_t)entry;

    /* argv (SO na 1a execucao): copia os argumentos para o FUNDO do stack do
       proprio processo (baixo endereco), longe dos frames que crescem do
       topo para baixo. ctx.arg1/arg2 = argc/argv chegam a entry em rdi/rsi.
       Os argumentos COPIADOS nao dependem mais do stack do criador. */
    if(argc>0 && argv){
        if(argc>64) argc=64;
        uint8_t *base=(uint8_t*)p->stack;
        uint64_t *arr=(uint64_t*)(void*)base;          /* char* arr[argc+1] */
        char *strp=(char*)(base + (size_t)(argc+1)*8);
        char *str_lmt=(char*)(base + PROC_STACK_SIZE - 4096);  /* reserva 4KB p/ args */
        int used=0;
        for(int i=0;i<argc;i++){
            const char *a = argv[i] ? argv[i] : "";
            size_t l=strlen(a); if(l>511) l=511;
            if(strp+l+1 > str_lmt) break;              /* limite de argumentos */
            memcpy(strp,a,l); strp[l]=0;
            arr[i]=(uint64_t)(uintptr_t)strp;
            strp += l+1;
            used++;
        }
        arr[used]=0;                                   /* argv[argc]=0 */
        p->ctx.arg1=(uint64_t)used;
        p->ctx.arg2=(uint64_t)(uintptr_t)arr;
    }

    /* espaco de enderecos proprio do processo (PML4, clone raso do kernel) */
    p->ctx.cr3=paging_new_address_space();
    /* slot reciclado (pos-GC) herda ran/style/hw_rsp do processo anterior;
       sem reset, o scheduler tratava o novo como "ja executou" e, se style==1,
       tentava iretq de um hw_rsp obsoleto. */
    p->ctx.ran=0;
    p->ctx.style=0;
    p->ctx.hw_rsp=0;
}

/* Cria um processo com stack proprio e contexto inicial apontando para entry.
   O entry recebe um ponteiro auxiliar (process_t*) para poder acessar seu pid.
   argc/argv vazio: entry e chamado sem argumentos (argc=0, argv=0). */
int proc_create_argv(const char *name, uint64_t rights, int argc, const char **argv,
                     void (*entry)(void), process_t *out){
    int idx=find_free();
    if(idx<0){ kprint("[kinesis] tabela cheia\n"); return -1; }
    process_t *p=&procs[idx];
    p->present=1;
    p->pid=next_pid++;
    strncpy(p->name, name?name:"anomimo", PROC_NAME_MAX-1); p->name[PROC_NAME_MAX-1]=0;
    p->rights=rights;
    p->state=PROC_READY;
    p->parent=0;
    p->blocked_on=0; p->ipc_avail=false; p->exit_code=0; p->scheduler_seq=0;
    p->is_user=0; p->user_kstack_top=0;
    p->umap_count=0;   /* ring-0: ponteiros confiados ao kernel */
    p->umap_heap=-1;   /* SYS_MMAP: nenhuma regiao de heap ainda */
    p->elf_rcount=0;   /* nenhuma regiao ELF reservada ainda */
    p->wait_pid=0; p->wait_code=0;
    proc_setup_stack(p, entry?entry:proc_crash_dummy, argc, argv);
    if(!p->stack){ p->present=0; return -2; }
    if(out) *out=*p;
    return p->pid;
}

int proc_create(const char *name, uint64_t rights, void (*entry)(void), process_t *out){
    return proc_create_argv(name, rights, 0, 0, entry, out);
}

/* ---- Ring 3 (Fase K) ---- */

extern void enter_user(void);            /* arch/x86_64/usermode.asm: iretq do frame */
extern void user_trampoline(void);       /* arch/x86_64/usermode.asm: jmp enter_user */
extern void gdt_set_kernel_stack(uint64_t rsp);

#define USER_STACK_SIZE (64*1024)
#define USER_VADDR_BASE 0x200000000000ULL   /* L4 idx 64: user space (PIC) */

/* Cria um processo que executa em RING 3.
   - 'entry' e o endereco virtual de entrada (ELF entry) ja carregado em memoria.
   - 'user_addr' / 'user_size': onde o codigo de usuario esta (identidade) para
     copiar e mapear como paginas de USUARIO no espaco do processo.
   A infraestrutura usa: paging_map_user (Fase P), enter_user/syscall_gate
   (usermode.asm) e um notebook de stack kernel por processo (kstack) que o
   scheduler define em tss.rsp0 antes de entrar no processo. */
int proc_create_user(const char *name, uint64_t rights,
                     uint64_t entry, void *user_code, size_t user_size,
                     process_t *out){
    return proc_create_user_args(name, rights, entry, user_code, user_size,
                                 0, NULL, out);
}

/* Variante de proc_create_user que, opcionalmente, monta argc/argv na stack
   de USUARIO (topo) e entrega argc (rdi) + argv (rsi) ao _start do app em
   ring 3 via enter_user. Sem args (argc==0), identico a proc_create_user.
   Layout na stack de usuario (descendo do topo): strings dos args, depois um
   array char* argv[n+1] terminado em NUL. O user RSP inicia logo abaixo do
   array; o app usa a stack livre abaixo e preserva o layout acima. */
#define APPS_ARGC_MAX 16

/* Calda comum de criacao de processo ring 3: aloca e mapeia a stack de
   usuario, monta argc/argv na stack, o frame iretq e o contexto, e registra
   as regioes (codigo + stack) no umap para validacao de ponteiros em syscalls.
   'pm' = espaco de enderecos ja criado; 'cb/cs' = regioes de codigo ja
   mapeadas (vaddr, len); max 8 regioes no umap. Retorna 0 ou erro < 0. */
static int proc_enter_user(process_t *p, uint64_t pm, uint64_t entry,
                           const uint64_t *cb, const uint64_t *cs, int cc,
                           int argc, const char **argv){
    uint64_t max_end=0;
    for(int i=0;i<cc;i++){ uint64_t e=cb[i]+cs[i]; if(e>max_end) max_end=e; }
    max_end=(max_end+0xFFF)&~(uint64_t)0xFFF;
    uint64_t user_stack_base = max_end + 0x10000;

    uint8_t *user_stack = pmm_alloc(USER_STACK_SIZE/0x1000);
    if(!user_stack) return -5;
    for(size_t i=0;i<USER_STACK_SIZE/0x1000;i++)
        paging_map_user(pm, p->pid, user_stack_base + i*0x1000,
                        (uint64_t)user_stack + i*0x1000,
                        0x7 | (1ULL<<63));        /* W^X: P|US|W|NX (grava, nao executa) */

    /* monta argc/argv na stack de usuario (zona fixa no TOPO) e define o user
       RSP logo abaixo dessa zona, preservando o layout e garantindo que o RSP
       fique SEMPRE dentro da regiao de stack mapeada. Sem args, nada e escrito
       (user RSP = topo da stack).
       LAYOUT (sem sobreposicao): o ARRAY de ponteiros fica COLADO no topo e as
       STRINGS ficam ABAIXO dele, descendo. Assim o array nunca pisa nas strings
       (bug anterior: array alinhado abaixo das strings crescendo para cima
       sobrescrevia as strings, que viravam lixo como 0x02). */
    #define APPS_ARGV_ZONE 0x1000   /* 4KB no topo da stack reservados ao layout */
    uint64_t user_rsp = user_stack_base + USER_STACK_SIZE;
    uint64_t argv_va = 0;
    if(argc > 0 && argv){
        if(argc > APPS_ARGC_MAX) argc = APPS_ARGC_MAX;
        uint64_t zone_base = user_stack_base + USER_STACK_SIZE - APPS_ARGV_ZONE;
        uint8_t *phys_top = (uint8_t*)((uintptr_t)user_stack + USER_STACK_SIZE);
        uint8_t *zone_floor = (uint8_t*)((uintptr_t)user_stack + USER_STACK_SIZE - APPS_ARGV_ZONE);
        /* array no topo (argc ponteiros + terminator nulo) */
        uint64_t *arr = (uint64_t*)(phys_top - ((uint64_t)argc + 1)*8);
        uint64_t a_va[APPS_ARGC_MAX];
        /* strings imediatamente ABAIXO do array, descendo */
        uint8_t *ps = (uint8_t*)arr;
        for(int i=argc-1;i>=0;i--){
            if(!argv[i]) continue;
            size_t len = strlen(argv[i]) + 1;
            if((uintptr_t)ps - len < (uintptr_t)zone_floor) break;
            ps -= len;
            memcpy(ps, argv[i], len);
            a_va[i] = user_stack_base + ((uintptr_t)ps - (uintptr_t)user_stack);
        }
        for(int i=0;i<argc;i++) arr[i] = a_va[i];
        arr[argc] = 0;
        argv_va = user_stack_base + ((uintptr_t)arr - (uintptr_t)user_stack);
        user_rsp = zone_base;            /* stack util abaixo da zona de layout */
    }
    /* monta o frame iretq -> ring 3 no TOPO do kernel stack, e um trampoline
       logo abaixo para quebrar o modelo 'ret' do context_switch_first.
       Frame iretq: o iretq restaura a partir do TOPO (rsp), na ordem
       [RIP][CS][RFLAGS][userRSP][SS] (RIP no endereco mais baixo).
       IMPORTANTE (bug de corrupcao multi-ring3): o frame tem 40 bytes e deve
       caber DENTRO do bloco do kernel stack (PROC_STACK_SIZE). Antes usavamos
       ktop = kst+16368, o que gravava o frame em [ktop..ktop+40) =
       [kst+16368..kst+16408) — 24 bytes ALEM do fim do bloco, corrompendo o
       header do proximo nó do heap (magic). Com 2+ processos ring-3 vivos o
       kmalloc do 2º app percorria o nó corrompido -> "[oikos] heap CORROMPIDO".
       Corrigido: frame ancorado no TOPO REAL do bloco (kend), 40 bytes abaixo;
       trampoline/argc/argv logo abaixo do frame — tudo dentro do bloco. */
    uintptr_t kst=(uintptr_t)p->stack;
    kst=(kst+15)&~(uintptr_t)15;
    uint64_t kend=(kst+PROC_STACK_SIZE)&~(uint64_t)15;   /* topo real do bloco */
    uint64_t frame_base=(kend-40)&~(uint64_t)7;          /* frame iretq (40B) */
    uint64_t *f=(uint64_t*)(uintptr_t)frame_base;
    f[0]=entry;                                 /* user RIP */
    f[1]=0x1B;                                  /* CS = user code (0x18 | RPL3) */
    f[2]=0x202;                                 /* RFLAGS (IF set) */
    f[3]=user_rsp;                              /* user RSP (topo do stack / abaixo do argv) */
    f[4]=0x23;                                  /* SS = user data (0x20 | RPL3) */
    /* ctx.rsp aponta para o espaco que o 'ret' vai pular: user_trampoline.
       apos o ret, rsp = frame_base, e user_trampoline pulou para enter_user que
       faz iretq lendo o frame em [frame_base..frame_base+40). */
    *(uint64_t*)(frame_base-8) = (uint64_t)&user_trampoline;
    /* argc/argv para enter_user: lidos de [rsp-24]/[rsp-16] (rsp=frame_base)
       antes do iretq rdi=argc, rsi=argv_va (0 quando sem args). */
    *(uint64_t*)(frame_base-24) = (uint64_t)argc;
    *(uint64_t*)(frame_base-16) = argv_va;
    p->ctx.rsp = frame_base - 8;
    p->ctx.rip = (uint64_t)&enter_user;         /* (debug) */
    p->ctx.rbp=0; p->ctx.rbx=0;
    p->ctx.r12=0;p->ctx.r13=0;p->ctx.r14=0;p->ctx.r15=0;
    p->ctx.r8=0; p->ctx.r9=0;p->ctx.r10=0;p->ctx.r11=0;
    p->ctx.arg1=(uint64_t)argc; p->ctx.arg2=argv_va;   /* rdi/rsi iniciais (enter_user) */
    p->ctx.style=0; p->ctx.hw_rsp=0;                    /* 1a execucao: entrada por ret */
    p->ctx.ran=0;                                        /* slot reciclado: herda 'ran' */

    /* O scheduler define tss.rsp0 = TOPO REAL do kernel stack ao despachar o
       processo, para que syscall/alloca de ring3 volte a um stack kernel valido. */
    p->is_user = 1;
    p->user_kstack_top = kend;

    /* regioes de usuario: codigo + stack (validacao de ponteiro em syscalls) */
    p->umap_count = 0;
    for(int i=0;i<cc;i++){ p->umap_base[p->umap_count]=cb[i]; p->umap_size[p->umap_count++]=cs[i]; }
    p->umap_base[p->umap_count]=user_stack_base;
    p->umap_size[p->umap_count++]=USER_STACK_SIZE;
    p->umap_heap = -1;   /* SYS_MMAP: heap comeca sem regiao; cresce sob demanda */
    p->elf_rcount=0;
    return 0;
}

int proc_create_user_args(const char *name, uint64_t rights,
                          uint64_t entry, void *user_code, size_t user_size,
                          int argc, const char **argv,
                          process_t *out){
    int idx=find_free();
    if(idx<0){ kprint("[kinesis] tabela cheia\n"); return -1; }
    process_t *p=&procs[idx];
    p->present=1;
    p->pid=next_pid++;
    strncpy(p->name, name?name:"anomimo", PROC_NAME_MAX-1); p->name[PROC_NAME_MAX-1]=0;
    p->rights=rights;
    p->state=PROC_READY;
    p->parent=current_proc ? current_proc->pid : 0;   /* filho do chamador (SYS_EXEC) */
    p->blocked_on=0; p->ipc_avail=false; p->exit_code=0; p->scheduler_seq=0;
    p->wait_pid=0; p->wait_code=0;

    /* kernel stack (usado para TSS.rsp0 ao entrar de ring3 e pelo syscall) */
    p->stack=kmalloc(PROC_STACK_SIZE);
    if(!p->stack){ p->present=0; return -2; }

    /* espaco de enderecos do processo */
    uint64_t pm = paging_new_address_space();
    if(!pm){ kfree(p->stack); p->present=0; return -3; }
    p->ctx.cr3 = pm;

    /* copia o codigo de usuario para paginas proprias e mapeia como user.
       Usa endereco virtual alto de USUARIO (0x200000000000 = L4 idx 64), acima
       do mapa identidade do kernel e num indice de PML4 que o kernel NAO mapeia
       (isolamento real por processo; nao colide com paginas supervisor/huge).
       Codigo PIC => funciona em qualquer vaddr. L4 idx em [0,255] = user space. */
    uint64_t user_vaddr = USER_VADDR_BASE;
    size_t code_pages = (user_size + 0xFFF)/0x1000;
    if(code_pages==0 || code_pages > 512) code_pages=512;
    uint8_t *code_phys = pmm_alloc(code_pages);
    if(!code_phys){ kfree(p->stack); p->present=0; return -4; }
    /* identidade-map nos permite copiar direto (kernel roda em anel 0):
       o PML4 do kernel mapeia code_phys na mesma faixa virtual (supervisor) */
    memcpy(code_phys, user_code, user_size);
    for(size_t i=0;i<code_pages;i++)
        paging_map_user(pm, p->pid, user_vaddr + i*0x1000,
                        (uint64_t)code_phys + i*0x1000,
                        0x5);                     /* W^X: P|US|X (executa, nao grava) */

    uint64_t cb[1]={user_vaddr}, cs[1]={code_pages*0x1000};
    int r=proc_enter_user(p, pm, entry, cb, cs, 1, argc, argv);
    if(r<0){ kfree(p->stack); p->present=0; return r; }

    if(out) *out=*p;
    return p->pid;
}

/* Cria um processo ring 3 a partir de segmentos ELF (exec_elf em anel 3,
   MARCO 1). Cada ureg_t vira uma regiao mapeada no espaco do processo em
   'vaddr' (pagina-alinhada, area de USUARIO = deve estar >= USER_VADDR_BASE
   para nunca colidir com o mapa identidade/supervisor compartilhado do
   kernel), com flags W^X derivadas de p_flags (PF_X -> RO+X, PF_W -> RW+NX,
   senao RO+NX). BSS (fileSz..memSz) e zerado. argc/argv entregues ao _start
   em ring 3. Retorna o pid ou negativo em erro. */
int proc_create_user_regions(const char *name, uint64_t rights, uint64_t entry,
                             const ureg_t *regs, int nregs,
                             int argc, const char **argv, process_t *out){
    if(!regs || nregs<=0 || nregs>7) return -9;
    int idx=find_free();
    if(idx<0){ kprint("[kinesis] tabela cheia\n"); return -1; }
    process_t *p=&procs[idx];
    p->present=1;
    p->pid=next_pid++;
    strncpy(p->name, name?name:"anomimo", PROC_NAME_MAX-1); p->name[PROC_NAME_MAX-1]=0;
    p->rights=rights;
    p->state=PROC_READY;
    p->parent=current_proc ? current_proc->pid : 0;
    p->blocked_on=0; p->ipc_avail=false; p->exit_code=0; p->scheduler_seq=0;
    p->wait_pid=0; p->wait_code=0;

    p->stack=kmalloc(PROC_STACK_SIZE);
    if(!p->stack){ p->present=0; return -2; }
    uint64_t pm = paging_new_address_space();
    if(!pm){ kfree(p->stack); p->present=0; return -3; }
    p->ctx.cr3 = pm;

    uint64_t ub[7], us[7]; int uc=0;
    for(int s=0;s<nregs;s++){
        const ureg_t *r=&regs[s];
        uint64_t base=r->vaddr & ~(uint64_t)0xFFF;
        uint64_t off=r->vaddr - base;
        if(base < USER_VADDR_BASE){
            char vb[96]; snprintf(vb,96,"[kinesis] segmento vaddr=0x%llx fora do user space\n",
                (unsigned long long)r->vaddr); kprint(vb);
            p->present=0; paging_free_address_space(pm); kfree(p->stack);
            return -10;
        }
        size_t npages=(off + r->memSz + 0xFFF)>>12;
        if(npages==0) npages=1;
        uint8_t *phys=pmm_alloc(npages);
        if(!phys){ p->present=0; paging_free_address_space(pm); kfree(p->stack); return -11; }
        memset(phys,0,npages*0x1000);
        if(r->fileSz && r->src) memcpy(phys+off, r->src, r->fileSz);
        /* W^X: PF_X=0x1, PF_W=0x2 (bit2=0x4 e PF_R). Se executa -> RX;
           se grava -> RW+NX; puro leitura -> RO+NX. */
        uint64_t pf = (r->flags & 0x1) ? 0x5 :
                      (r->flags & 0x2) ? (0x7|(1ULL<<63)) : (0x1|(1ULL<<63));
        for(size_t pi=0;pi<npages;pi++)
            paging_map_user(pm, p->pid, base+pi*0x1000,
                            (uint64_t)phys+pi*0x1000, pf);
        if(uc<7){ ub[uc]=base; us[uc]=npages*0x1000; uc++; }
    }

    int r=proc_enter_user(p, pm, entry, ub, us, uc, argc, argv);
    if(r<0){ p->present=0; kfree(p->stack); return r; }
    if(out) *out=*p;
    return p->pid;
}

/* ---- Scheduler (round-robin, cooperativo + tick) ---- */

void sched_init(void){
    scheduler_running=false;
    kprint("[kinesis] scheduler pronto\n");
}

int sched_is_running(void){ return scheduler_running?1:0; }

void sched_add(process_t *p){
    /* jah esta na tabela; marca como pronto */
    if(p) p->state=PROC_READY;
}

static int next_ready_from(int start){
    for(int c=0;c<MAX_PROCS;c++){
        int i=(start + c) % MAX_PROCS;
        if(procs[i].present && procs[i].state==PROC_READY) return i;
    }
    return -1;
}

/* Prepara o processador para entrar no processo de destino: para um processo
   de usuario (ring 3), seta tss.rsp0 para o topo do SEU kernel stack, para que
   qualquer syscall/IRQ vindo de ring3 volte a um stack kernel valido. */
static void sched_prep(process_t *to){
    if(to && to->is_user && to->user_kstack_top)
        gdt_set_kernel_stack(to->user_kstack_top);
}

/* Le o bit IF (interrupt flag) do RFLAGS atual. */
static inline bool flags_if(void){
    unsigned long f;
    __asm__ volatile("pushfq; pop %0":"=r"(f));
    return (f>>9)&1;
}

/* Auxiliar de troca: desliga interrupcoes durante o context_switch (a troca
   nao e reentrante/atomica) e, AO RETOMAR, restaura IF apenas se estava ligado
   na entrada. Se a troca veio de um ISR (IF desligado pela interrupt gate), nao
   religamos aqui: o iretq do ISR restaure de volta ao processo. Isso evita
   interrupcao aninhada no meio do retorno do ISR (que corrompia o frame).
   FIX (bug-fix 7): o flip global (to->state/current_index/current_proc) fica
   DENTRO do cli — antes ficava com IF=1 (um PIT entre current_proc=to e o cli
   fazia o ISR estacionar o ALVO com frame do processo que RODAVA de fato,
   corrompendo ctx.hw_rsp/style). */
static void sched_switch(process_t *from, process_t *to, int next_i){
    bool if_was = flags_if();
    /* ATOMICIDADE TOTAL do switch: o `cli` vem ANTES do snprintf/[sw]/prep,
       fechando a janela em que um PIT estacionava o processo (style=1) NO
       MEIO da cadeia C de um yield cooperativo (IF=1). Ao retomar, o frame
       parkado re-entrava num snprintf com a va_list sobrescrita por bytes de
       mensagens ['/pk'/'sw'] -> #GP/#PF ring-0 (classe bug-fix 2/3). */
    disable_interrupts();
    sched_prep(to);
    { char db[128]; snprintf(db,128,"[sw] t=%llu from='%s'p%u->'%s'p%u run=%d if=%d\n",
        (unsigned long long)ticks, from?from->name:"?", from?from->pid:0,
        to->name, to->pid, (int)to->ctx.ran, if_was?1:0); kprint(db); }
    to->state=PROC_RUNNING;
    current_index=next_i;
    current_proc=to;
    context_switch(&from->ctx, &to->ctx);   /* retorna quando re-agendado */
    if(if_was) enable_interrupts();
    if(current_proc) current_proc->state=PROC_RUNNING;
}

/* Escolhe o proximo processo READY a partir do indice atual e faz a troca. */
void sched_yield(void){
    proc_gc_run();                         /* teardown de zombies (nunca o atual) */
    if(!scheduler_running) return;
    if(current_proc && current_proc->state==PROC_RUNNING) current_proc->state=PROC_READY;
    // avanca "ponteiro" a partir do atual (que recuou a READY)
    int next_i = next_ready_from(current_index+1);
    if(next_i<0){
        { char db[96]; snprintf(db,96,"[yield] cur='%s'(pid%u) st=%d idx=%d SEM_READY\n",
            current_proc?current_proc->name:"?", current_proc?current_proc->pid:0,
            current_proc?current_proc->state:-1, current_index); kprint(db); }
        if(current_proc) current_proc->state=PROC_RUNNING;   /* so temos este */
        return;
    }
    process_t *from = current_proc;
    process_t *to = &procs[next_i];
    if(from==to){ if(from) from->state=PROC_RUNNING; return; }
    if(from){
        from->state=PROC_READY;
        sched_switch(from, to, next_i);
    } else {
        sched_prep(to);
        disable_interrupts();
        to->state=PROC_RUNNING;
        current_index=next_i;
        current_proc=to;
        context_switch_first(to);
        enable_interrupts();
    }
    /* quando re-agendado: continua daqui */
}

/* Preempcao por tick: decide (sem trocar de contexto) qual processo deve rodar
   a seguir e devolve o alvo ao STUB do ISR (idt.asm), que faz a troca no nivel
   do frame (estilo IRQ): o processo atual e estacionado com seu bloco de GPRs +
   frame da CPU e o alvo e retomado via iretq — sem re-entrar em cadeia C.
   Retorna 0 quando nao ha troca (current continua rodando). */
process_t *sched_preempt_pick(void){
    proc_gc_run();
    if(!current_proc || !scheduler_running) return 0;
    if(current_proc->state!=PROC_RUNNING) return 0;
    process_t *from=current_proc;
    from->state=PROC_READY;
    int next_i=next_ready_from(current_index+1);
    if(next_i<0 || next_i==current_index){
        from->state=PROC_RUNNING;
        g_isr_from=0;
        return 0;
    }
    process_t *to=&procs[next_i];
    to->state=PROC_RUNNING;
    current_index=next_i;
    current_proc=to;
    g_isr_from=from;                /* stub estaciona o processo saindo */
    sched_prep(to);                 /* ring-3: tss.rsp0 antes do iretq */
    { char db[96]; snprintf(db,96,"[pk] t=%llu '%s'p%u->'%s'p%u\n",
        (unsigned long long)ticks, from->name, from->pid, to->name, to->pid); kprint(db); }
    return to;
}

/* Bloqueia o processo atual (espera IPC ou outra condicao). */
void sched_block(void){
    if(!current_proc) return;
    current_proc->state=PROC_BLOCKED;
    int next_i=next_ready_from(current_index+1);
    if(next_i<0){
        /* nada mais pronto: se unico, desbloqueia auxiliar; senao desiste */
        kprint("[kinesis] sched_block: nada pronto!\n");
        current_proc->state=PROC_RUNNING;
        return;
    }
    process_t *from=current_proc;
    process_t *to=&procs[next_i];
    from->state=PROC_BLOCKED;
    sched_switch(from, to, next_i);
}

/* Desbloqueia um processo (torna READY). */
void sched_wakeup(uint32_t pid){
    process_t *p=proc_get(pid);
    if(p && p->state==PROC_BLOCKED){
        p->state=PROC_READY; p->blocked_on=0;
    }
}

void sched_tick(void){
    /* preempcao: se em estado RUNNING de um processo de usuario, chama yield
       forcando troca. Em prestacao sem herdando de ISR; por seguranca, o
       tick so sinaliza e a troca real ocorre via sched_yield em tempo seguro.*/
    /* Faz a troca aqui — dado que o ISR salvou o frame completo e o
       scheduler (versao preemptiva) restaura via frame. Por ora, apenas
       registra; a preempcao full-frame e tratada no irq handler. */
    if(current_proc && scheduler_running && current_proc->state==PROC_RUNNING){
        process_t *from=current_proc;
        from->state=PROC_READY;
        int next_i=next_ready_from(current_index+1);
        if(next_i>=0 && next_i!=current_index){
            process_t *to=&procs[next_i];
            sched_switch(from, to, next_i);
        }
    }
}

void sched_run_first(void){
    scheduler_running=true;
    int i=next_ready_from(0);
    if(i<0){ kprint("[kinesis] nada para rodar\n"); return; }
    disable_interrupts();
    current_index=i;
    current_proc=&procs[i];
    current_proc->state=PROC_RUNNING;
    kprint("[kinesis] iniciando scheduler\n");
    sched_prep(current_proc);
    context_switch_first(current_proc);
    /* nunca retorna; processos chamam exit/yield */
}

void proc_exit(int code){
    if(!current_proc){ halt(); }
    /* grava o codigo de saida no PAI (que pode estar em proc_waitpid); o pai
       usa isso mesmo depois do filho ser coletado pelo GC (o campo sobrevive
       no PCB do pai). Tambem acorda o pai se ele estava bloqueado no wait. */
    process_t *parent = proc_get(current_proc->parent);
    if(parent && parent->present){
        parent->wait_pid = current_proc->pid;
        parent->wait_code = (uint32_t)code;
        sched_wakeup(current_proc->parent);
    }
    char buf[80]; snprintf(buf,80,"[kinesis] processo %s (pid %u) encerrou (codigo %d)\n",
        current_proc->name, current_proc->pid, code);
    kprint(buf);
    current_proc->exit_code=code;
    current_proc->state=PROC_TERMINATED;
    /* nao libera a stack aqui (ainda estamos em cima dela); gc futuro. */
    /* escolhe proximo READY; como o atual terminou, restaura o proximo direto */
    int next_i=next_ready_from(current_index+1);
    if(next_i<0){ kprint("[kinesis] todos os processos terminaram; halt\n"); halt(); }
    process_t *to=&procs[next_i];
    { char db[260]; snprintf(db,260,"[exit] cur_idx=%d -> next_i=%d '%s' st=%d ran=%d | rsp=0x%llx rip=0x%llx cr3=0x%llx hw_rsp=0x%llx style=%d\n",
        current_index, next_i, to->name, to->state, (int)to->ctx.ran,
        (unsigned long long)to->ctx.rsp, (unsigned long long)to->ctx.rip,
        (unsigned long long)to->ctx.cr3,
        (unsigned long long)to->ctx.hw_rsp, (int)to->ctx.style); kprint(db); }
    to->state=PROC_RUNNING;
    current_proc=to;
    current_index=next_i;
    sched_prep(to);                 /* ring-3: seta tss.rsp0 antes de entrar */
    disable_interrupts();
    context_switch_first(to);   /* restaura 'to' e ret executa em seu entry-referencia */
    halt();
}

/* ---- GC de zombies (Auditoria W^X/teardown) ----
   Quando um processo termina, liberamos o que ele criou: kernel stack,
   espaco de enderecos (PML4 + paginas de usuario) e regioes ELF reservadas.
   Nunca libera o espaco de enderecos do processo em EXECUCAO agora (o GC
   roda em contexto seguro — dentro do current — e compara com o CR3 ativo). */
void proc_gc_run(void){
    uint64_t active_cr3 = paging_current_cr3();
    for(int i=0;i<MAX_PROCS;i++){
        process_t *p=&procs[i];
        if(!p->present || p->state!=PROC_TERMINATED) continue;
        char gb[96]; snprintf(gb,96,"[gc] coletando '%s' pid %u\n", p->name, p->pid); kprint(gb);
        if(p->stack){
            kfree(p->stack);
            p->stack=0;
        }
        if(p->ctx.cr3 && p->ctx.cr3!=active_cr3){
            paging_free_address_space(p->ctx.cr3);
            p->ctx.cr3=0;
        }
        /* regioes ELF reservadas no mapa identidade (elf.c) */
        for(int r=0;r<p->elf_rcount && r<8;r++){
            if(p->elf_rpages[r]) pmm_free((void*)(uintptr_t)p->elf_rbase[r], (size_t)p->elf_rpages[r]);
        }
        p->elf_rcount=0;
        p->present=0;
    }
}

/* Espera um processo filho terminar (SYS_WAITPID). Retorna o codigo de saida.
   Se o pid nao existe / nao e filho, retorna -1 imediatamente. Senão bloqueia
   (sched_block) até o proc_exit do filho gravar wait_code e despertar este pai. */
int proc_waitpid(uint32_t pid){
    process_t *parent = current_proc;
    if(!parent) return -1;
    process_t *kid = proc_get(pid);
    if(!kid || !kid->present || kid->parent != parent->pid) return -1;
    for(;;){
        if(parent->wait_pid == pid){
            int code = (int)parent->wait_code;
            parent->wait_pid = 0;
            return code;
        }
        if(!kid->present || kid->state==PROC_TERMINATED){
            /* saiu e, por algum motivo, sem despertar: tenta ler o codigo */
            if(parent->wait_pid==pid){
                parent->wait_pid=0;
                return (int)parent->wait_code;
            }
            return -1;
        }
        sched_block();
        kid = proc_get(pid);
    }
}

void proc_list_print(void){
    char buf[96];
    kprint("[kinesis] processos:\n");
    static const char *st[]={"CRIADO","PRONTO","RODANDO","BLOQUEADO","TERMINADO"};
    for(int i=0;i<MAX_PROCS;i++){
        if(!procs[i].present || procs[i].state==PROC_TERMINATED) continue;
        int s=procs[i].state; if(s<0||s>4) s=0;
        snprintf(buf,96,"  pid %u %-14s [%s] caps=%llx\n",
            procs[i].pid, procs[i].name, st[s], (unsigned long long)procs[i].rights);
        kprint(buf);
    }
}
