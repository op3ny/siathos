#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../boot/thaisboot/bootinfo.h"

#define FS_NAME_MAX 256

typedef struct {
    void *addr;
    uint64_t width, height, pitch;
    uint16_t bpp;
} thais_fb_t;

typedef uint64_t cap_id_t;
#define CAP_NONE 0
/* Capabilities do sistema real (Phases C+) */
#define CAP_FB_DRAW       (1ULL<<0)
#define CAP_AISTHESIS     (1ULL<<1)
#define CAP_KINESIS_SPAWN (1ULL<<2)
#define CAP_NOMOS_WRITE   (1ULL<<3)
#define CAP_EMPORION      (1ULL<<4)
#define CAP_IDIOS_WRITE   (1ULL<<5)
#define CAP_FS_READ       (1ULL<<6)
#define CAP_FS_WRITE      (1ULL<<7)
#define CAP_USER_ADMIN    (1ULL<<8)
#define CAP_EXEC          (1ULL<<9)
#define CAP_IPC           (1ULL<<10)
#define CAP_SYSCALL       (1ULL<<11)
#define CAP_DEV_IO        (1ULL<<12)
#define CAP_ALL 0xFFFFFFFFFFFFFFFFULL

/* Estados de processo (Phases D+) */
#define PROC_CREATED   0
#define PROC_READY     1
#define PROC_RUNNING   2
#define PROC_BLOCKED   3
#define PROC_TERMINATED 4

#define PROC_NAME_MAX 64
#define MAX_CAPS 8
#define MAX_PROCS 64
typedef struct {
    uint64_t rights;
} capset_t;
/* Backwards-compat cap container */
typedef struct { cap_id_t id; uint64_t rights; bool valid; } cap_t;
typedef struct {
    uint32_t pid;
    char name[PROC_NAME_MAX];
    cap_t caps[MAX_CAPS];
    uint32_t cap_count;
    bool active;
} proc_t;

/* ---- Processos / Scheduler (Phases D-F) ---- */
#define PROC_STACK_SIZE (16*1024)
typedef struct cpu_context {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
    uint64_t rbp,rbx;
    uint64_t rip,rsp;
    uint64_t cr3;              /* espaco de enderecos proprio do processo (PML4) */
    uint64_t arg1,arg2;        /* rdi/rsi do ENTRY (argc/argv) — so na 1a execucao */
    uint64_t ran;              /* 0 = ainda nao executou (carrega arg1/arg2) */
    /* Classe de retomada (fix do stall pos-exit):
       style=0 (C): resolve via 'ret' re-entrando na cadeia C (yield voluntario
                    e 1a execucao). ctx.rsp = ponto de retorno.
       style=1 (IRQ): resolve via iretq direto do frame salvo pelo stub do ISR
                    (preempcao). ctx.hw_rsp = base do bloco de GPRs + frame CPU
                    que o stub deixou no kernel stack do processo. */
    uint8_t  style;
    uint64_t hw_rsp;
} cpu_context_t;
typedef struct process {
    int       present;
    uint32_t  pid;
    char      name[PROC_NAME_MAX];
    uint64_t  rights;              /* capability set */
    int       state;               /* PROC_* */
    uint8_t   *stack;              /* kernel stack */
    cpu_context_t ctx;             /* saved context */
    uint32_t  parent;
    /* IPC */
    uint32_t  blocked_on;          /* pid being awaited in ipc_send (0 = none) */
    bool      ipc_avail;
    int       exit_code;
    int       scheduler_seq;       /* for potential priority/fairness */
    int       is_user;             /* ring 3 (1) vs ring 0 (0) */
    uint64_t  user_kstack_top;     /* topo do kernel stack p/ tss.rsp0 (ring 3) */
    /* Regioes de memoria do processo (para validacao de ponteiros em syscalls).
       Guarda faixas [base, base+size) que podem ser acessadas pelo processo.
       So preenchido para processos ring-3 (is_user); ring-0 confia no kernel. */
    uint64_t  umap_base[8];
    uint64_t  umap_size[8];
    int       umap_count;
    /* Regioes ELF reservadas no mapa identidade (elf.c): base/paginas a
       liberar no exit (GC de zombie). Indice 0 = sistema (usa 0xFF em elf_count). */
    uint64_t  elf_rbase[8];
    uint64_t  elf_rpages[8];
    int       elf_rcount;
    /* Espera de filho (SYS_WAITPID, MARCO 3): pid esperado + codigo de saida
       gravado AQUI no PAI pelo proc_exit do filho (sobrevive ao GC do filho). */
    uint32_t  wait_pid;
    uint32_t  wait_code;
    /* SYS_MMAP (ABI 1.3): indice (em umap_*) da regiao do heap, ou -1. A
       regiao e um bloco contiguo que cresce: base em umap_base[slot], tamanho
       atual em umap_size[slot] (incrementado a cada SYS_MMAP). */
    int       umap_heap;
    /* ABI 1.4 (drivers USB/MMIO): regiao "device IO" — BARs de hardware
       mapeados por SYS_DEV_MAP. Base fixa alta do user space; us=bytes
       ja mapeados a partir da base. Nao entra em umap_* (nao copiamos
       buffers pela regiao; o driver acessa diretamente). O teardown NAO
       libera os frames (marcados PTE_AVAIL_MMIO). */
    uint64_t  umap_devio_base;
    uint64_t  umap_devio_used;
} process_t;
void proc_init(void);
int proc_create(const char *name, uint64_t rights, void (*entry)(void), process_t *out);
int proc_create_argv(const char *name, uint64_t rights, int argc, const char **argv,
                     void (*entry)(void), process_t *out);
int proc_create_user(const char *name, uint64_t rights, uint64_t entry,
                     void *user_code, size_t user_size, process_t *out);
int proc_create_user_args(const char *name, uint64_t rights, uint64_t entry,
                          void *user_code, size_t user_size,
                          int argc, const char **argv, process_t *out);
/* Regiao ELF mapeada em ring 3 por proc_create_user_regions (exc.c).
   vaddr: p_vaddr (pode nao ser pagina-alinhada); src/fileSz: bytes copiados
   do arquivo; memSz: total incl. bss (zerado); flags: p_flags (PF_X/PF_W). */
typedef struct {
    uint64_t vaddr;
    const void *src;
    size_t fileSz;
    size_t memSz;
    uint64_t flags;
} ureg_t;
int proc_create_user_regions(const char *name, uint64_t rights, uint64_t entry,
                             const ureg_t *regs, int nregs,
                             int argc, const char **argv, process_t *out);
uint32_t proc_pid_of(process_t *p);
process_t* proc_get(uint32_t pid);
process_t* proc_at(int idx);
void proc_exit(int code);
int proc_waitpid(uint32_t pid);
void proc_list_print(void);
void sched_init(void);
void sched_yield(void);
void sched_add(process_t *p);
void sched_block(void);
void sched_wakeup(uint32_t pid);
void sched_tick(void);
void sched_run_first(void);
int sched_is_running(void);
process_t *sched_preempt_pick(void);
extern process_t *current_proc;
extern process_t *g_isr_from;
void context_switch(cpu_context_t *from, cpu_context_t *to);
void context_switch_first(process_t *p);

#define MAX_CONTRACTS 32
typedef struct {
    char nome[64];
    char caminho[FS_NAME_MAX];     /* pasta protegida pelo contrato */
    bool leitura;
    bool escrita;
    bool execucao;
    bool voluntario;               /* aceitacao voluntaria requerida */
    bool revogavel;
    bool ativo;
    uint64_t caps_req;
} contract_t;

#define PHRASE_COUNT 4
extern const char *boot_phrases[PHRASE_COUNT];

void fb_init(thais_fb_t *fb);
void fb_clear(uint32_t color);
void fb_draw_rect(int x,int y,int w,int h,uint32_t color);
void fb_draw_char(int x,int y,char c,uint32_t fg,uint32_t bg);
void fb_draw_char_scaled(int x,int y,char c,uint32_t fg,uint32_t bg,int scale);
void fb_draw_text(int x,int y,const char *s,uint32_t fg,uint32_t bg);
void fb_draw_text_scaled(int x,int y,const char *s,uint32_t fg,uint32_t bg,int scale);
void fb_draw_box_centered(int w,int h,uint32_t bg,uint32_t border);
void fb_console_init(thais_fb_t *fb);
void fb_console_activate(void);
void fb_console_panic(void);
bool fb_console_boot_active(void);
void fb_console_putc(char c);
void fb_console_write(const char *s);
void fb_console_clear(void);
uint32_t fb_color(uint8_t r,uint8_t g,uint8_t b);

void splash_show(thais_fb_t *fb);
void splash_set_progress(int percent);
void splash_set_spinner(int frame);

void serial_init(void);
void serial_write(const char *s);
void serial_putc(char c);
void kprint(const char *s);
void outb_port(uint16_t port,uint8_t v);
void outw_port(uint16_t port,uint16_t v);
void outl_port(uint16_t port,uint32_t v);
uint8_t inb_port(uint16_t port);
uint16_t inw_port(uint16_t port);
uint32_t inl_port(uint16_t port);
void slow_down(void);

void pmm_init(thais_boot_info_t *bi);
void pmm_reserve(uint64_t base, uint64_t size);
extern uint64_t boot_kernel_phys;
void *pmm_alloc(size_t pages);
void pmm_free(void *addr, size_t pages);
bool pmm_range_free(uint64_t base, size_t pages);
uint64_t pmm_total_mem(void);
uint64_t pmm_free_mem(void);
void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void heap_walk_quick(void);

void cap_init(void);
bool cap_grant(uint32_t pid, uint64_t rights);
bool cap_has(uint32_t pid, uint64_t right);
bool cap_check(uint32_t pid, uint64_t right);
bool cap_revoke(uint32_t pid, uint64_t rights);
void kinesis_init(void);
uint32_t kinesis_spawn(const char *name, uint64_t caps);
uint32_t kinesis_register(const char *name, uint64_t caps);
void kinesis_list(void);
process_t* kinesis_get(uint32_t pid);

void synallagma_init(void);
void synallagma_load_defaults(void);
void synallagma_start_all(void);
void synallagma_load_contracts(void);
int  synallagma_scan_contracts(void);
contract_t* synallagma_contract_for(const char *path);
bool synallagma_guards_folder(const char *path);
int  synallagma_count(void);
bool consent_prompt_contracts(void);
bool synallagma_consented(const char *caminho);
void consent_grant(int idx);
contract_t* synallagma_contract_at(int idx);
void synallagma_list_console(void);

void keyboard_init(void);
uint64_t usb_xhci_bar(void);     /* pci.c: BAR0 fisico do controlador xHCI, ou 0 */
int xhci_init(uint64_t bar);     /* xhci.c: inicializa o controlador no BAR0 (0=ok) */
int xhci_enumerate(void);        /* xhci.c: porta + Enable Slot (slot ou <0) */
int xhci_active(void);           /* xhci.c: 1 se o controlador foi inicializado */
int xhci_kbd_present_p(void);    /* xhci.c: 1 se teclado HID enumerado no xHCI */
int xhci_kbd_read_hid(uint8_t *buf, uint32_t max); /* xhci.c: le report HID (bloqueia) */
void usb_init(void);
int usb_enumerate_hid_keyboard(void);
int usb_kbd_read_report(uint8_t *report);
void usb_kbd_set_report(const uint8_t *report, uint8_t len);
int usb_kbd_parse_report(const uint8_t *report, char *out_char);
int usb_kbd_getc(char *c);
void usb_irq_handler(void);
bool keyboard_has_key(void);
char keyboard_getc(void);
char keyboard_getc_nonblock(bool *has);
void keyboard_get_line(char *buf, size_t max, bool echo, bool mask);

#define FS_MAX_FILES 128
#define FS_MAX_DATA (4*1024*1024)
#define FS_ERROR (-1)
#define FS_OK 0
typedef enum {
    FS_TYPE_FILE=0,
    FS_TYPE_DIR,
    FS_TYPE_SYMLINK
} fs_type_t;
typedef struct {
    char path[FS_NAME_MAX];
    uint8_t *data;
    size_t size;
    size_t capacity;
    fs_type_t type;
    bool used;
    uint8_t owner;          /* owner user index (AUTH_USER_MAX sized) or 0xFF for system */
    uint64_t caps_req;      /* capability required to access */
    bool world_read;        /* readable by anyone */
    bool world_write;       /* writable by anyone */
} fs_node_t;
void fs_init(void);
void fs_init_defaults(void);
int fs_create(const char *path, fs_type_t type);
int fs_write(const char *path, const void *data, size_t size);
int fs_read(const char *path, void *buf, size_t max, size_t *out_size);
bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
int fs_remove(const char *path);
int fs_copy(const char *src, const char *dst);
bool mod_is_kormi(const char *cfg, size_t cfg_size);
int fs_move(const char *src, const char *dst);
int fs_list(const char *dir, char *out, size_t out_max);
fs_node_t* fs_get(const char *path);

/* mods embutidos (ABI 1.5): acesso a tabela global de modulos userspace p/
   execucao DIRETA dos bytes embutidos, sem passar pelo ramfs (enabler da
   migracao fs→servico: boot/exec desacoplado do FS). */
size_t fs_mod_count(void);
const char *fs_mod_name(size_t i);
const unsigned char *fs_mod_elf(size_t i, size_t *out_size);
const unsigned char *fs_mod_cfg(size_t i, size_t *out_size);
bool fs_mod_is_kormi(size_t i);

/* ---- VFS ---- */
void vfs_init(void);
int vfs_normalize(const char *cwd, const char *in, char *out, size_t out_max);
int vfs_open(const char *cwd, const char *path, char *out_full);
int vfs_create(const char *cwd, const char *path, fs_type_t type);
int vfs_read(const char *cwd, const char *path, void *buf, size_t max, size_t *out_size);
int vfs_write(const char *cwd, const char *path, const void *data, size_t size);
int vfs_remove(const char *cwd, const char *path);
int vfs_copy(const char *cwd, const char *src, const char *dst);
int vfs_move(const char *cwd, const char *src, const char *dst);
int vfs_list(const char *cwd, const char *path, char *out, size_t out_max);
int vfs_mkdir(const char *cwd, const char *path);
bool vfs_check_cap(const char *path, uint64_t need);

#define AUTH_USER_MAX 16
#define AUTH_NAME_MAX 32
#define AUTH_HASH_SIZE 65            /* 64 hex (32 bytes PBKDF2) + NUL */
#define AUTH_SALT_SIZE 33            /* 32 hex (16 bytes salt) + NUL */
#define AUTH_HASH_ITER 100000        /* custo PBKDF2-HMAC-SHA256 */
typedef struct {
    char name[AUTH_NAME_MAX];
    char hash[AUTH_HASH_SIZE];
    char salt[AUTH_SALT_SIZE];
    uint64_t caps;
    bool active;
    bool is_admin;
} auth_user_t;
void auth_init(void);
bool auth_create_user(const char *name, const char *password, uint64_t caps, bool admin);
bool auth_verify(const char *name, const char *password);
auth_user_t* auth_find(const char *name);
void auth_list(void);
void auth_hash_password(const char *password, const char *salt, char out_hex[65]);
bool auth_change_password(const char *name, const char *oldpw, const char *newpw);
bool auth_change_password_admin(const char *name, const char *newpw);
bool auth_user_delete(const char *name);
int auth_list_text(char *buf, size_t max);
int auth_user_count(void);

/* ABI 1.5 — tickets de sessao (delegacao de auth p/ servico ring 3):
   o servico 'authd' valida credenciais/politica e EMITE um ticket no kernel
   (exige CAP_USER_ADMIN). SYS_SESSION_LOGIN(a3=tok) consome o ticket e
   monta a sessao usando os caps registrados, sem tocar no store do kernel. */
bool auth_ticket_issue(const char *name, uint64_t caps, uint64_t *tok_out);
auth_user_t* auth_ticket_consume(const char *name, uint64_t tok);

void login_main(thais_fb_t *fb);
auth_user_t* login_result_user(void);
void thais_main(thais_boot_info_t *bi);
void login_set_user(auth_user_t *u);
void thais_sh_main(thais_fb_t *fb);
void thais_exec_line(char *l);              /* exposto p/ teste embutido (TEST=1) */
int auth_user_index_current(void);

int exec_file(const char *path);
int exec_file_args(const char *path, int argc, const char **argv);
void edit_file(const char *path);

void *memset(void *s,int c,size_t n);
void *memcpy(void *d,const void *s,size_t n);
void *memmove(void *d,const void *s,size_t n);
int strcmp(const char *a,const char *b);
int strncmp(const char *a,const char *b,size_t n);
size_t strlen(const char *s);
char *strcpy(char *d,const char *s);
char *strncpy(char *d,const char *s,size_t n);
char *strchr(const char *s,int c);
char *strrchr(const char *s,int c);
int snprintf(char *buf,size_t n,const char *fmt,...);
uint64_t rand64(void);
void halt(void);
void reboot(void);
void poweroff(void);
void delay_busy(uint64_t iters);
void yield_to_scheduler(void);
uint64_t rdtsc64(void);

/* ---- IDT / IRQ / Timer (Phase G) ---- */
void idt_init(void);
void idt_load(void);
void irq_install_handler(int irq, void (*handler)(void));
void pit_init(uint32_t hz);
uint16_t pit_latch_read(void);
uint32_t pit_us_since(uint16_t latch_prev);
void pit_ack(void);
extern volatile uint64_t ticks;
void pic_remap(void);
void pic_eoi(uint8_t irq);
process_t *irq_common_handler(uint64_t int_no, uint64_t err);
void isr_common_handler(uint64_t int_no, uint64_t error, uint64_t *regs);
void enable_interrupts(void);
void disable_interrupts(void);

/* ---- Syscalls (Phase J, ABI 1.0) ---- */
#define SYS_EXIT       0
#define SYS_WRITE      1
#define SYS_READ       2
#define SYS_EXEC       3
#define SYS_IPC_SEND   4
#define SYS_IPC_RECV   5
#define SYS_YIELD      6
#define SYS_FS_READ    7
#define SYS_FS_WRITE   8
#define SYS_FS_LIST    9
#define SYS_GETPID     10
#define SYS_IPC_REPLY  11
#define SYS_SYSINFO    12
#define SYS_UPTIME     13
#define SYS_GETPPID    14
#define SYS_PROC_LIST  15
#define SYS_AUTH_COUNT 17
#define SYS_AUTH_CREATE 18   /* bootstrap: cria admin (so quando count==0) */
#define SYS_AUTH_VERIFY 19   /* (user, pass) -> 1 ok / 0 falhou */
#define SYS_CONTRACT_LIST 20 /* (buf, max) -> lista textual dos contratos ativos */
#define SYS_SESSION_LOGIN 21 /* (user, aceitar_todos) -> 1 sessao / 0 negado */
#define SYS_CONSOLE     22   /* a1=acao: 0=clear, 1=reboot, 2=poweroff */
#define SYS_FS_REMOVE   23   /* (path) -> remove arquivo/dir (anairesis) */
#define SYS_FS_CREATE   16   /* (path,type) -> cria arquivo/dir (ktisis) */
#define SYS_WAITPID     24   /* (pid) -> codigo de saida do filho (bloqueia) */
#define SYS_SVC_REGISTER 25  /* (name,buf) -> registra servico IPC deste processo */
#define SYS_SVC_QUERY   26  /* (fbuf,max,name) -> pid do servico pelo nome */
#define SYS_AUTH_LIST   27  /* (buf,max) -> lista nomes de usuarios */
#define SYS_AUTH_DELETE 28  /* (name) -> admin remove usuario */
#define SYS_AUTH_CHANGEPW 29 /* (user,old,new) -> troca senha */
#define SYS_AUTH_CREATE_USER 30 /* (name,pass) -> admin cria usuario nao-admin */
#define SYS_FB_DRAW     31  /* (op,fb_cmd_t*) -> desenha no framebuffer (CAP_FB_DRAW) */
#define SYS_IO_PORT     32  /* (op,port,val) -> I/O de portas (CAP_DEV_IO): op 0=in8 1=in16 2=in32 3=out8 4=out16 5=out32 */
#define SYS_MMAP        33  /* (paginas) -> base VA contigua (RW|NX) do heap do processo */
#define SYS_DEV_MAP     34  /* (phys,size) -> VA (RW|NX|MMIO) mapeado p/ driver (CAP_DEV_IO) */
#define SYS_V2P         35  /* (va) -> PA fisica da pagina (DMA) — usuario que mapeou via SYS_MMAP/DEV_MAP */
#define SYS_AUTH_TICKET_ISSUE 36 /* ABI 1.5: (name,caps,&tok) -> token de sessao emitido pelo servico 'auth' (CAP_USER_ADMIN); o kernel so consome via SYS_SESSION_LOGIN(a3=tok) */
#define SYS_AUTH_HASH   37  /* ABI 1.5: (pw,salt-hex,&out-hex) -> PBKDF2-HMAC-SHA256 com IRQs off (primitiva computacional p/ authd; CAP_USER_ADMIN) */
#define SYS_COUNT       38   /* numero total de syscalls (ABI 1.5) */

/* ABI 1.4 (drivers): endereco virtual base da regiao "device IO" dos processos
   ring 3 (BARs MMIO mapeados por SYS_DEV_MAP). Fica no topo do user space.
   PTE_AVAIL_MMIO marca PTE cujo frame NAO e do PMM (hardware): o teardown de
   espaco de enderecos nao libera esses frames (nao pode dar pmm_free num BAR). */
#define USER_DEVIO_BASE  0x00007ff000000000ULL
#define PTE_AVAIL_MMIO   (1ULL<<11)
void syscall_init(void);
uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);

/* ---- Apps embutidos ring 3 (Fase K/ABI 1.0) ---- */
int apps_try_run(const char *path, uint32_t *out_pid);
int apps_try_run_args(const char *path, int argc, const char **argv, uint32_t *out_pid);
int apps_spawn_fetch(void);   /* teste/boot: spawna o fetch no arranque */

/* ---- Informacao do sistema (ABI 1.0, sysinfo) ---- */
#define SYSINFO_OS_NAME_MAX 8
#define SYSINFO_PROC_NAME_MAX 64
typedef struct {
    uint64_t memory_total;      /* bytes */
    uint64_t memory_free;       /* bytes */
    uint64_t memory_used;       /* bytes */
    uint32_t cpu_count;
    uint32_t process_count;     /* processos present (<=MAX_PROCS) */
    uint64_t uptime_ms;         /* tempo desde o boot (ticks do PIT, 100Hz) */
    uint64_t ticks;             /* ticks brutos do PIT */
} system_info_t;
typedef struct {
    uint32_t pid;
    char     name[SYSINFO_PROC_NAME_MAX];
    int      state;             /* PROC_* */
    uint8_t  is_user;
    int      exit_code;
} proc_info_t;
int sysinfo_get(system_info_t *out);
int sysinfo_proc_list(proc_info_t *out, int max);

/* ---- Framebuffer / desenho (ABI 1.2, SYS_FB_DRAW) ----
   cmd->op: 0=clear (color), 1=rect(x,y,w,h,color), 2=pixel(x,y,color),
            3=text(x,y,str,fg,bg), 4=char(x,y,ch,fg,bg). */
#define FB_CMD_CLEAR  0
#define FB_CMD_RECT   1
#define FB_CMD_PIXEL  2
#define FB_CMD_TEXT   3
#define FB_CMD_CHAR   4
typedef struct {
    int32_t  op;
    int32_t  x, y, w, h;
    uint32_t color;
    uint32_t color2;            /* bg (text/char) */
    const char *text;
} fb_cmd_t;
#define FB_CMD_TEXT_MAX 128
int fb_get_dim(uint32_t *w, uint32_t *h);

/* ---- IPC (Phase H) ---- */
#define IPC_MSG_MAX 4096
#define IPC_QUEUE_MAX 64
typedef struct {
    uint32_t sender;
    uint32_t receiver;
    uint32_t type;
    uint64_t payload[IPC_MSG_MAX/8];
    size_t   size;
} ipc_message_t;
void ipc_init(void);
int ipc_send(uint32_t to, uint32_t type, const void *data, size_t size);
int ipc_receive(uint32_t *from, uint32_t *type, void *buf, size_t max, size_t *out_size);
int ipc_try_receive(uint32_t *from, uint32_t *type, void *buf, size_t max, size_t *out_size);
int ipc_reply(uint32_t to, uint32_t type, const void *data, size_t size);
int ipc_send_blocking(uint32_t to, uint32_t type, const void *data, size_t size);
#define IPC_TYPE_KBD_GET 10  /* odigos_pliktrologiou (servico 'kbd'): pedido de proxima tecla */

/* ---- GDT / Ring 3 (Phase K) ---- */
void gdt_init(void);
void load_gdtr(void);
void load_idt(void);
void gdt_set_kernel_stack(uint64_t rsp);

/* ---- Paging / VM (Phase P) ---- */
void paging_init(void);
uint64_t paging_new_address_space(void);
int paging_map_user(uint64_t pml4, uint32_t dst_pid, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t paging_unmap_user(uint64_t pml4, uint64_t virt);
uint64_t paging_phys_of_user(uint64_t pml4, uint64_t virt, int *is_mmio);
void paging_free_address_space(uint64_t pml4);
uint64_t paging_current_cr3(void);
void paging_load(uint64_t pml4);
void paging_map_device_identity(uint64_t phys, uint64_t bytes);
void proc_gc_run(void);

/* ---- ELF loader (Phase O) ---- */
int exec_elf(const char *path, uint32_t *out_pid);
int exec_elf_args(const char *path, int argc, const char **argv, uint32_t *out_pid);
/* execucao de modulo embutido (ABI 1.5) — mesma pipeline do ELF loader, mas os
   bytes vêm da tabela s_mods[] (fs.c), sem fs_read. Retorna FS_OK/-1. */
int exec_embedded_args(const char *name, int argc, const char **argv, uint32_t *out_pid);
/* Caminho unico de execucao de app ring 3 com args: usado por SYS_EXEC e por
   hooks de teste (TEST=1). 'path' já normalizado (ex.: "/praxis/ls"). */
int exec_user_path(const char *path, int argc, const char *const *argv, uint32_t *out_pid);

/* ---- Servicos IPC (MARCO 9) ---- */
void svc_init(void);
int  svc_register(const char *name);
int  svc_query(const char *name, uint32_t *out_pid);
