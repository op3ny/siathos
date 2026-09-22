/* spud.h — library minimima para apps userspace do Siaht OS (MARCO 3+).
   Syscalls via int 0x80 (gate de interrupt), sem libc.
   Incluido em todos os apps: spoudazo, praxia, ls, cat, echo, ps, mem. */
#ifndef SPUD_H
#define SPUD_H
#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_EXEC        3
#define SYS_IPC_SEND    4
#define SYS_IPC_RECV    5
#define SYS_YIELD       6
#define SYS_FS_READ     7
#define SYS_FS_WRITE    8
#define SYS_FS_LIST     9
#define SYS_GETPID     10
#define SYS_IPC_REPLY  11
#define SYS_SYSINFO    12
#define SYS_UPTIME     13
#define SYS_GETPPID    14
#define SYS_PROC_LIST  15
#define SYS_FS_CREATE  16
#define SYS_AUTH_COUNT 17
#define SYS_AUTH_CREATE 18
#define SYS_AUTH_VERIFY 19
#define SYS_CONTRACT_LIST 20
#define SYS_SESSION_LOGIN 21
#define SYS_CONSOLE    22
#define SYS_FS_REMOVE  23
#define SYS_WAITPID    24
#define SYS_SVC_REGISTER 25
#define SYS_SVC_QUERY  26
#define SYS_AUTH_LIST  27
#define SYS_AUTH_DELETE 28
#define SYS_AUTH_CHANGEPW 29
#define SYS_AUTH_CREATE_USER 30
#define SYS_FB_DRAW     31
#define SYS_IO_PORT     32
#define SYS_MMAP        33
#define SYS_DEV_MAP     34   /* ABI 1.4 */
#define SYS_V2P         35   /* ABI 1.4 */
#define SYS_AUTH_TICKET_ISSUE 36 /* ABI 1.5: (name,caps,&tok) -> token de sessao (CAP_USER_ADMIN, so o servico 'auth') */
#define SYS_AUTH_HASH   37  /* ABI 1.5: (pw,salt-hex,&out-hex) -> PBKDF2 com IRQs off (primitiva p/ authd; CAP_USER_ADMIN) */

typedef struct {
    uint64_t memory_total, memory_free, memory_used;
    uint32_t cpu_count, process_count;
    uint64_t uptime_ms, ticks;
} spud_sysinfo_t;

typedef struct {
    uint32_t pid;
    char     name[64];
    int      state;
    uint8_t  is_user;
    int      exit_code;
} spud_procinfo_t;

/* ---- syscall wrapper ---- */
static long spud_sys(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5){
    long r;
    register uint64_t a5reg __asm__("r8") = a5;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "c"(a4), "r"(a5reg)
                     : "memory");
    return r;
}

/* ---- utilidades minimas (sem libc) ---- */
static size_t spud_len(const char *s){ size_t n=0; while(s[n]) n++; return n; }
static int spud_cmp(const char *a, const char *b){
    while(*a && *a==*b){ a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void spud_ncpy(char *d, const char *s, size_t max){
    size_t i=0;
    while(i+1<max && s[i]){ d[i]=s[i]; i++; }
    d[i]=0;
}
static void spud_ncat(char *d, const char *s, size_t max){
    size_t i=0; while(d[i]) i++;
    while(i+1<max && *s){ d[i++]=*s++; }
    d[i]=0;
}
static int spud_memcmp(const void *a, const void *b, size_t n){
    const unsigned char *x=(const unsigned char*)a, *y=(const unsigned char*)b;
    for(size_t i=0;i<n;i++){ if(x[i]!=y[i]) return 1; }
    return 0;
}
static const char *spud_strstr(const char *hay, const char *needle){
    if(!hay || !needle || !needle[0]) return hay;
    for(size_t i=0; hay[i]; i++){
        size_t j=0;
        while(needle[j] && hay[i+j]==needle[j]) j++;
        if(!needle[j]) return hay+i;
    }
    return 0;
}

/* ---- E/S basica ---- */
static void spud_write(const char *s){
    spud_sys(SYS_WRITE, 1, (uint64_t)(uintptr_t)s, spud_len(s), 0, 0);
}
static void spud_write_ch(char c){
    spud_sys(SYS_WRITE, 1, (uint64_t)(uintptr_t)&c, 1, 0, 0);
}

static void spud_print_dec(uint64_t v){
    char b[24]; int i=(int)sizeof(b); b[--i]=0;
    if(v==0) b[--i]='0';
    while(v){ b[--i]=(char)('0'+(v%10)); v/=10; }
    spud_write(&b[i]);
}

/* teclado com poll batch (16 reads por yield — hardening do bug-fix 7;
   remocao total do yield piorou (v7), manter o pacificado) */
static int spud_getc(void){
    char c=0;
    for(;;){
        for(int i=0;i<16;i++){
            long r=spud_sys(SYS_READ, (uint64_t)(uintptr_t)&c, 1, 0, 0, 0);
            if(r==1) return (unsigned char)c;
        }
        spud_sys(SYS_YIELD, 0, 0, 0, 0, 0);
    }
}

/* leitura de linha: Enter encerra, backspace apaga.
   Estado do loop em variaveis globais VOLATILE: o retorno de syscall deste
   kernel pode zerar registradores callee-saved (classe bugfix 2/3) — manter
   n/buf/flags em memoria evita corromper a leitura entre spud_getc/write. */
static volatile size_t spud_rl_n;
static volatile size_t spud_rl_max;
static volatile char *spud_rl_buf;
static volatile int spud_rl_echo, spud_rl_mask;
static void spud_readline(char *buf, size_t max, int echo, int mask){
    spud_rl_buf=buf; spud_rl_max=max; spud_rl_echo=echo; spud_rl_mask=mask;
    spud_rl_n=0;
    for(;;){
        int c=spud_getc();
        if(c=='\n' || c=='\r'){ buf[spud_rl_n]=0; return; }
        if(c==8 || c==127){
            if(spud_rl_n>0){ spud_rl_n--; if(spud_rl_echo||spud_rl_mask) spud_write("\b \b"); }
            continue;
        }
        if(c < 32 || c > 126) continue;
        if(spud_rl_n+1 >= spud_rl_max){ buf[spud_rl_n]=0; return; }
        if(spud_rl_echo) spud_write_ch((char)c);
        if(spud_rl_mask)  spud_write_ch('*');
        buf[spud_rl_n++]=(char)c;
    }
}

/* resolve caminho: absoluto como esta; relativo comeca em /praxis */
static void spud_resolve(const char *arg, const char *cwd, char *out, size_t max){
    if(!arg || !*arg){ spud_ncpy(out, cwd?cwd:"/praxis", max); return; }
    if(arg[0]=='/'){ spud_ncpy(out, arg, max); return; }
    spud_ncpy(out, cwd?cwd:"/praxis", max);
    spud_ncat(out, "/", max);
    spud_ncat(out, arg, max);
}

/* imprime hello, args e sai (app padrao de teste) */
static void spud_hello(int argc, char **argv){
    spud_write("[hello] args: ");
    for(int i=1;i<argc;i++){
        if(i>1) spud_write_ch(' ');
        spud_write(argv[i]);
    }
    spud_write("\n");
}

/* exec externo + espera (MARCO 3) */
static long spud_exec_wait(const char *path, int argc, const char **argv){
    long pid = spud_sys(SYS_EXEC,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uint32_t)argc,
        argv ? (uint64_t)(uintptr_t)argv : 0,
        0, 0);
    if(pid > 0){
        long code = spud_sys(SYS_WAITPID, (uint64_t)(uint32_t)pid, 0, 0, 0, 0);
        return code;
    }
    return -1;
}

/* ---- IPC de servico (MARCO 9: fsd/devd + clientes) ---- */
#define IPC_TYPE_PING    1  /* ping -> resposta "pong" */
#define IPC_TYPE_READ    2  /* fsd: payload=path -> resposta=dados do arquivo */
#define IPC_TYPE_LIST    3  /* fsd: payload=path -> resposta=listagem */
#define IPC_TYPE_UPTIME  4  /* devd: -> resposta="uptime=<ms>" */
#define IPC_TYPE_SYSINFO 5  /* devd: -> resposta=string com memoria */
#define IPC_TYPE_KBD_GET 10 /* odigos_pliktrologiou (servico 'kbd'): pedido de proxima tecla -> 1 byte (0=nenhuma) */
#define IPC_TYPE_FS_CREATE 11 /* fsd: req=path -> resp='1'/'0' (cria arquivo) */
#define IPC_TYPE_FS_WRITE 12  /* fsd: req="path\n<dados>" (texto, sem NUL) -> resp='1'/'0' */
#define IPC_TYPE_FS_REMOVE 13 /* fsd: req=path -> resp='1'/'0' */
#define IPC_TYPE_FS_AUDIT 14  /* fsd: -> resp=texto das ultimas ops (trilho do servico) */
#define IPC_TYPE_FS_MKDIR 15  /* fsd: req=path -> resp='1'/'0' (cria diretorio) */
#define IPC_TYPE_AUTH_COUNT 20 /* authd (servico 'auth'): -> resp=decimal de contas */
#define IPC_TYPE_AUTH_CREATE 21 /* authd: req=name\0pass\0 -> resp=1/0 (bootstrap: 1o usuario soberano) */
#define IPC_TYPE_AUTH_LOGIN 22 /* authd: req=name\0pass\0 -> resp=16-hex do ticket (verificado) ou "0" */
#define IPC_TYPE_AUTH_LIST 23  /* authd: -> resp=lista textual de contas */
#define IPC_TYPE_AUTH_DELETE 24 /* authd: req=name -> resp=1/0 (requer sessao soberana) */
#define IPC_TYPE_AUTH_CHANGEPW 25 /* authd: req=user\0old\0new\0 -> resp=1/0 */
#define IPC_TYPE_AUTH_CREATE_USER 26 /* authd: req=name\0pass\0 -> resp=1/0 (requer sessao soberana) */
#define IPC_TYPE_SYN_LIST 30     /* synd (servico 'synd'): req=filtro(opcional) -> resp=texto dos contratos vigentes (espelho IPC_TYPE_SYN_LIST do fsd) */
#define IPC_TYPE_SYN_READ 31     /* synd: req=caminho -> resp=contrato detalhado (mesmo formato list, contrato unico) ou '0' */
#define IPC_TYPE_SYN_CONSENT 32  /* synd: req=caminho -> resp='1'/'0' (consentimento voluntario na sessao; kernel aplica a regra) */
#define IPC_TYPE_SYN_REVOKE 33   /* synd: req=caminho -> resp='1'/'0' (revoga o consentimento; trilho de auditoria do servico) */
#define IPC_TYPE_SYN_AUDIT 34    /* synd: -> resp=texto userspace do trilho de auditoria (quem consentiu/revogou o que) */

static long spud_ipc_send(uint32_t to, uint32_t type, const void *data, size_t size){
    return spud_sys(SYS_IPC_SEND, to, type, (uint64_t)(uintptr_t)data, size, 0);
}
static long spud_ipc_recv(uint32_t *from, uint32_t *type, void *buf, size_t max){
    return spud_sys(SYS_IPC_RECV, (uint64_t)(uintptr_t)buf, max,
                    (uint64_t)(uintptr_t)from, (uint64_t)(uintptr_t)type, 0);
}
static long spud_ipc_reply(uint32_t to, uint32_t type, const void *data, size_t size){
    return spud_sys(SYS_IPC_REPLY, to, type, (uint64_t)(uintptr_t)data, size, 0);
}

/* resolve o pid de um servico registrado (retry com yield — o servico pode
   ainda estar registrando quando o cliente acorda). */
static long spud_svc_query_retry(const char *name){
    for(int tries=0;tries<2000;tries++){
        uint32_t pid=0;
        long q=spud_sys(SYS_SVC_QUERY, (uint64_t)(uintptr_t)name,
                        (uint64_t)(uintptr_t)&pid, 0, 0, 0);
        if(q==pid && pid>0) return pid;
        spud_sys(SYS_YIELD, 0, 0, 0, 0, 0);
    }
    return -1;
}

/* formato de servidor: registra o nome e atende requisicoes ate o fim do
   processo (loop IPC_RECV -> handler -> IPC_REPLY). o handler recebe o
   pedido e devolve a resposta (tecla locate). */
typedef int (*spud_svc_handler_t)(uint32_t from, uint32_t type,
                                  const char *req, char *resp, size_t resp_max);
/* SYS_IPC_RECV e nao-bloqueante: com a mailbox vazia devolve <0 e o servico
   cede (SYS_YIELD) ate chegar mensagem — evita o park profundo da cadeia C
   dentro de syscall (classe bugfix 2/3). */
static long spud_svc_serve(const char *name, spud_svc_handler_t handler){
    long reg=spud_sys(SYS_SVC_REGISTER, (uint64_t)(uintptr_t)name, 0, 0, 0, 0);
    if(reg!=0) return -1;
    char req[512], resp[4096];
    for(;;){
        uint32_t from=0, type=0;
        long got=spud_ipc_recv(&from, &type, req, sizeof(req));
        if(got<0){
            spud_sys(SYS_YIELD, 0, 0, 0, 0, 0);
            continue;
        }
        req[got]=0;
        size_t rl=0;
        if(handler){
            int n=handler(from, type, req, resp, sizeof(resp));
            if(n<0) continue;
            rl=(size_t)n;
        } else {
            spud_ncpy(resp,"?",sizeof(resp)); rl=1;
        }
        spud_ipc_reply(from, type, resp, rl);
    }
}

/* recebe com retry+yield (IPC nao-bloqueante): espera a resposta de uma
   requisicao ja enviada. SYS_YIELD em anel 3 e tick-only (nao chaveia),
   entao o orcamento precisa cobrir uma volta completa de agendamento
   (todos os servicos + kernel) — senao o cliente esgota as tentativas
   antes dos servidores rodarem e recebe falso-negativo. */
static long spud_ipc_recv_retry(uint32_t *from, uint32_t *type,
                                void *buf, size_t max){
    for(long tries=0;tries<50000;tries++){
        long got=spud_ipc_recv(from, type, buf, max);
        if(got>=0) return got;
        spud_sys(SYS_YIELD, 0, 0, 0, 0, 0);
    }
    return -1;
}

/* ---- Cliente do servico 'auth' (authd, ABI 1.5) ----
   IPC nao-bloqueante com retry+yield ao servico; se o authd nao estiver
   registrado, FALLBACK aos syscalls do kernel (store classico). Cada boot
   usa um unico fluxo (authd OU syscalls), decidido pelo primeiro RPC. */
static long spud_auth_rpc(uint32_t type, const void *payload, size_t plen,
                          char *out, size_t out_max){
    long svc=spud_svc_query_retry("auth");
    if(svc<=0) return -1;
    if(spud_ipc_send((uint32_t)svc, type, payload, plen)!=0) return -1;
    uint32_t from=0, rt=0;
    long got=spud_ipc_recv_retry(&from, &rt, out, out_max ? out_max-1 : 0);
    if(got<0) return -1;
    if(rt!=type || from!=(uint32_t)svc) return -1;
    out[got]=0;
    return got;
}

/* empacota 2 ou 3 campos NUL-separados no request do authd */
static size_t spud_auth_pack(char *req, size_t req_max,
                             const char *a, const char *b, const char *c){
    size_t n=0;
    const char *f[3]={ a, b?b:"", c?c:"" };
    for(int k=0;k<(c?3:(b?2:1));k++){
        const char *p=f[k];
        while(*p && n+1<req_max) req[n++]=*p++;
        if(n+1<req_max) req[n++]=0; else { req[req_max-1]=0; return n; }
    }
    return n;
}

static long spud_auth_count(void){
    char out[32];
    long got=spud_auth_rpc(IPC_TYPE_AUTH_COUNT, 0, 0, out, sizeof(out));
    if(got>=0){
        long v=0;
        for(long i=0; i<got && i<31; i++){
            if(out[i]<'0' || out[i]>'9') break;
            v=v*10 + (out[i]-'0');
        }
        return v;
    }
    return spud_sys(SYS_AUTH_COUNT, 0, 0, 0, 0, 0);
}

static long spud_auth_create(const char *user, const char *pass){
    char req[128], out[16];
    size_t n=spud_auth_pack(req, sizeof(req), user, pass, 0);
    long got=spud_auth_rpc(IPC_TYPE_AUTH_CREATE, req, n, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_AUTH_CREATE, (uint64_t)(uintptr_t)user, (uint64_t)(uintptr_t)pass, 0, 0, 0);
}

/* login: valida no authd e recebe o ticket (hex). 'tok'=0 no fallback
   (kernel valida no store e a sessao usa o caminho classico). */
static long spud_auth_login(const char *user, const char *pass, uint64_t *tok){
    if(tok) *tok=0;
    char req[128], out[64];
    size_t n=spud_auth_pack(req, sizeof(req), user, pass, 0);
    long got=spud_auth_rpc(IPC_TYPE_AUTH_LOGIN, req, n, out, sizeof(out));
    if(got>=0){
        if(got==1 && out[0]=='0') return 0;      /* credenciais invalidas */
        if(got==16){                             /* 16-hex do ticket */
            uint64_t v=0;
            for(int i=0;i<16;i++){
                char c=out[i]; uint64_t d;
                if(c>='0'&&c<='9') d=(uint64_t)(c-'0');
                else if(c>='a'&&c<='f') d=(uint64_t)(c-'a'+10);
                else if(c>='A'&&c<='F') d=(uint64_t)(c-'A'+10);
                else return 0;
                v=(v<<4)|d;
            }
            if(tok) *tok=v;
            return 1;
        }
        return 0;
    }
    /* authd ausente: kernel valida (store classico) */
    return spud_sys(SYS_AUTH_VERIFY, (uint64_t)(uintptr_t)user, (uint64_t)(uintptr_t)pass, 0, 0, 0);
}

static long spud_auth_list(char *buf, size_t max){
    long got=spud_auth_rpc(IPC_TYPE_AUTH_LIST, 0, 0, buf, max);
    if(got>=0) return 0;
    return spud_sys(SYS_AUTH_LIST, (uint64_t)(uintptr_t)buf, (uint64_t)max, 0, 0, 0);
}

static long spud_auth_delete(const char *name){
    char out[16];
    long got=spud_auth_rpc(IPC_TYPE_AUTH_DELETE, name, name?spud_len(name):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_AUTH_DELETE, (uint64_t)(uintptr_t)name, 0, 0, 0, 0);
}

static long spud_auth_changepw(const char *who, const char *oldp, const char *newp){
    char req[196], out[16];
    size_t n=spud_auth_pack(req, sizeof(req), who, oldp, newp);
    long got=spud_auth_rpc(IPC_TYPE_AUTH_CHANGEPW, req, n, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_AUTH_CHANGEPW,
        (uint64_t)(uintptr_t)who, (uint64_t)(uintptr_t)oldp, (uint64_t)(uintptr_t)newp, 0, 0);
}

static long spud_auth_create_user(const char *name, const char *pass){
    char req[128], out[16];
    size_t n=spud_auth_pack(req, sizeof(req), name, pass, 0);
    long got=spud_auth_rpc(IPC_TYPE_AUTH_CREATE_USER, req, n, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_AUTH_CREATE_USER,
        (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)pass, 0, 0, 0);
}

/* ---- Cliente do servico 'fsd' (ABI 1.5) ----
   IPC nao-bloqueante com retry+yield ao servico; se o fsd nao estiver
   registrado, FALLBACK aos syscalls SYS_FS_* do kernel (o kernel fornece o
   primitivo). AUDIT nao tem fallback — e o trilho proprio do servico. */
static long spud_fsd_rpc(uint32_t type, const void *payload, size_t plen,
                         char *out, size_t out_max){
    long svc=spud_svc_query_retry("fsd");
    if(svc<=0) return -1;
    if(spud_ipc_send((uint32_t)svc, type, payload, plen)!=0) return -1;
    uint32_t from=0, rt=0;
    long got=spud_ipc_recv_retry(&from, &rt, out, out_max ? out_max-1 : 0);
    if(got<0) return -1;
    if(rt!=type || from!=(uint32_t)svc) return -1;
    out[got]=0;
    return got;
}

static long spud_fsd_read(const char *path, char *out, size_t max){
    long got=spud_fsd_rpc(IPC_TYPE_READ, path, path?spud_len(path):0, out, max);
    if(got>=0) return got;
    return spud_sys(SYS_FS_READ, (uint64_t)(uintptr_t)path,
                    (uint64_t)(uintptr_t)out, (uint64_t)max, 0, 0);
}

static long spud_fsd_list(const char *path, char *out, size_t max){
    long got=spud_fsd_rpc(IPC_TYPE_LIST, path, path?spud_len(path):0, out, max);
    if(got>=0) return got;
    /* SYS_FS_LIST retorna 0 em sucesso (conteudo ja no buffer) — normaliza
       para bytes lidos (contrato dos helpers base). */
    long r=spud_sys(SYS_FS_LIST, (uint64_t)(uintptr_t)path,
                    (uint64_t)(uintptr_t)out, (uint64_t)max, 0, 0);
    if(r<0) return r;
    if(max) out[max-1]=0;
    return (long)spud_len(out);
}

static long spud_fsd_create(const char *path){
    char out[16];
    long got=spud_fsd_rpc(IPC_TYPE_FS_CREATE, path, path?spud_len(path):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_FS_CREATE, (uint64_t)(uintptr_t)path, 0, 0, 0, 0)==0 ? 1 : 0;
}

static long spud_fsd_write(const char *path, const void *data, size_t size){
    char req[256+128], out[16];
    size_t n=0;
    const char *p=path;
    while(p && *p && n+1<sizeof(req)) req[n++]=*p++;
    if(n+1<sizeof(req)) req[n++]='\n';
    const unsigned char *d=(const unsigned char*)data;
    for(size_t i=0;i<size && n+1<sizeof(req);i++) req[n++]=(char)d[i];
    long got=spud_fsd_rpc(IPC_TYPE_FS_WRITE, req, n, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_FS_WRITE, (uint64_t)(uintptr_t)path,
                    (uint64_t)(uintptr_t)data, (uint64_t)size, 0, 0)==0 ? 1 : 0;
}

static long spud_fsd_remove(const char *path){
    char out[16];
    long got=spud_fsd_rpc(IPC_TYPE_FS_REMOVE, path, path?spud_len(path):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_FS_REMOVE, (uint64_t)(uintptr_t)path, 0, 0, 0, 0)==0 ? 1 : 0;
}

static long spud_fsd_mkdir(const char *path){
    char out[16];
    long got=spud_fsd_rpc(IPC_TYPE_FS_MKDIR, path, path?spud_len(path):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return spud_sys(SYS_FS_CREATE, (uint64_t)(uintptr_t)path, 1, 0, 0, 0)==0 ? 1 : 0;
}

static long spud_fsd_audit(char *out, size_t max){
    return spud_fsd_rpc(IPC_TYPE_FS_AUDIT, 0, 0, out, max);
}

/* ---- Cliente do servico 'synd' (ABI 1.6) ----
   IPC nao-bloqueante com retry+yield ao servico (dono da politica de
   contratos em userspace). LIST tem fallback syscall (SYS_CONTRACT_LIST,
   mesmo formato de texto do kernel); READ/CONSENT/REVOKE/AUDIT sao valor
   proprio do servico — sem fallback (sem primitivo kernel correspondente),
   devolvem -1 quando o 'synd' nao esta registrado. */
static long spud_synd_rpc(uint32_t type, const char *payload, size_t plen,
                          char *out, size_t out_max){
    long svc=spud_svc_query_retry("synd");
    if(svc<=0) return -1;
    if(spud_ipc_send((uint32_t)svc, type, payload, plen)!=0) return -1;
    uint32_t from=0, rt=0;
    long got=spud_ipc_recv_retry(&from, &rt, out, out_max ? out_max-1 : 0);
    if(got<0) return -1;
    if(rt!=type || from!=(uint32_t)svc) return -1;
    out[got]=0;
    return got;
}

static long spud_synd_list(const char *filtro, char *out, size_t max){
    long got=spud_synd_rpc(IPC_TYPE_SYN_LIST, filtro, filtro?spud_len(filtro):0,
                           out, max);
    if(got>=0) return got;
    /* fallback: o mesmo texto do kernel (lista humana de contratos) */
    long r=spud_sys(SYS_CONTRACT_LIST, (uint64_t)(uintptr_t)out,
                    (uint64_t)max, 0, 0, 0);
    if(r<0) return r;
    if(max) out[max-1]=0;
    return (long)spud_len(out);
}

static long spud_synd_read(const char *path, char *out, size_t max){
    return spud_synd_rpc(IPC_TYPE_SYN_READ, path, path?spud_len(path):0,
                         out, max);
}

static long spud_synd_consent(const char *path){
    char out[16];
    long got=spud_synd_rpc(IPC_TYPE_SYN_CONSENT, path,
                           path?spud_len(path):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return -1;
}

static long spud_synd_revoke(const char *path){
    char out[16];
    long got=spud_synd_rpc(IPC_TYPE_SYN_REVOKE, path,
                           path?spud_len(path):0, out, sizeof(out));
    if(got>=0) return out[0]=='1';
    return -1;
}

static long spud_synd_audit(char *out, size_t max){
    return spud_synd_rpc(IPC_TYPE_SYN_AUDIT, 0, 0, out, max);
}

/* ---- FS de userspace: SEMPRE via servico (svc-first), fallback syscall ----
   Contratos dos helpers base (o que os apps usam):
     read/list -> bytes lidos ou -1; write/create_file/create_dir/remove -> 0 ok / -1.
   O servico 'fsd' e a autoridade; SYS_FS_* do kernel e o primitivo de
   fallback quando o servico nao esta registrado (mesmo padrao do auth). */
static long spud_fs_read(const char *path, char *out, size_t max){
    return spud_fsd_read(path, out, max);
}

static long spud_fs_list(const char *path, char *out, size_t max){
    return spud_fsd_list(path, out, max);
}

static long spud_fs_write(const char *path, const void *data, size_t size){
    return spud_fsd_write(path, data, size)==1 ? 0 : -1;
}

static long spud_fs_create_file(const char *path){
    return spud_fsd_create(path)==1 ? 0 : -1;
}

static long spud_fs_create_dir(const char *path){
    return spud_fsd_mkdir(path)==1 ? 0 : -1;
}

static long spud_fs_remove(const char *path){
    return spud_fsd_remove(path)==1 ? 0 : -1;
}

/* ---- helpers SYS_FB_DRAW (ABI ring-3 para drivers/GUI/Doom) ---- */
#define SPUD_FB_CLEAR  0
#define SPUD_FB_RECT   1
#define SPUD_FB_PIXEL  2
#define SPUD_FB_TEXT   3
#define SPUD_FB_CHAR   4
typedef struct __attribute__((packed)){
    int32_t op, x, y, w, h;
    uint32_t color;
    uint32_t color2;
    const char *text;
} spud_fb_t;
static void spud_fb_draw(spud_fb_t *cmd){
    spud_sys(SYS_FB_DRAW,(uint64_t)(uintptr_t)cmd,0,0,0,0);
}
static void spud_fb_clear(uint32_t color){
    spud_fb_t c={SPUD_FB_CLEAR,0,0,0,0,color,0,0}; spud_fb_draw(&c);
}
static void spud_fb_rect(int x,int y,int w,int h,uint32_t color){
    spud_fb_t c={SPUD_FB_RECT,x,y,w,h,color,0,0}; spud_fb_draw(&c);
}
static void spud_fb_pixel(int x,int y,uint32_t color){
    spud_fb_t c={SPUD_FB_PIXEL,x,y,0,0,color,0,0}; spud_fb_draw(&c);
}
static void spud_fb_text(int x,int y,const char *s,uint32_t fg,uint32_t bg){
    spud_fb_t c={SPUD_FB_TEXT,x,y,0,0,fg,bg,s}; spud_fb_draw(&c);
}

/* ---- helpers SYS_IO_PORT (ABI ring-3 para drivers) ---- */
static inline uint8_t spud_inb(uint16_t port){
    return (uint8_t)spud_sys(SYS_IO_PORT,0,(uint64_t)port,0,0,0);
}
static inline uint16_t spud_inw(uint16_t port){
    return (uint16_t)spud_sys(SYS_IO_PORT,1,(uint64_t)port,0,0,0);
}
static inline uint32_t spud_inl(uint16_t port){
    return (uint32_t)spud_sys(SYS_IO_PORT,2,(uint64_t)port,0,0,0);
}
static inline void spud_outb(uint16_t port,uint8_t val){
    spud_sys(SYS_IO_PORT,3,(uint64_t)port,(uint64_t)val,0,0);
}
static inline void spud_outw(uint16_t port,uint16_t val){
    spud_sys(SYS_IO_PORT,4,(uint64_t)port,(uint64_t)val,0,0);
}
static inline void spud_outl(uint16_t port,uint32_t val){
    spud_sys(SYS_IO_PORT,5,(uint64_t)port,(uint64_t)val,0,0);
}

/* ---- MMIO/DMA userspace (ABI 1.4, SYS_DEV_MAP/SYS_V2P) ----
   spud_dev_map: mapeia o BAR fisico 'phys' (paginas alinhadas) numa VA da
   regiao device-IO do processo (P|US|W|NX). Requer CAP_DEV_IO.
   Retorna a VA base (0 = erro).
   spud_v2p: endereco fisico (para DMA) de uma VA mapeada do processo
   (heap/SYS_MMAP ou devio). 0 = nao mapeada. */
static inline void *spud_dev_map(uint64_t phys, uint64_t size){
    long va=spud_sys(SYS_DEV_MAP,(uint64_t)phys,(uint64_t)size,0,0,0);
    if(va<=0) return 0;
    return (void*)(uintptr_t)va;
}
static inline uint64_t spud_v2p(void *va){
    uint64_t pa=(uint64_t)spud_sys(SYS_V2P,(uint64_t)(uintptr_t)va,0,0,0,0);
    return pa==(uint64_t)-1 ? 0 : pa;
}

/* ---- malloc userspace (ABI 1.3, SYS_MMAP) ----
   Heap com free-list first-fit sobre paginas mapeadas sob demanda (SYS_MMAP)
   no espaco do processo, COM COALESCING por boundary tags: cada bloco guarda
   o espelho do size num footer imediatamente apos o payload, permitindo fundir
   um bloco livre com seus vizinhos adjacentes na regiao linear (contigua:
   SYS_MMAP estende sempre a partir do topo do heap). */
#define SPUD_HEAP_CHUNK (64*1024)
#define SPUD_HDRSZ 24u                    /* header: size(8)+free(4)+next(8) */
#define SPUD_RBLK  8u                     /* footer (boundary tag espelho) */
typedef struct spud_hdr {
    size_t size;                          /* bytes uteis do payload */
    int    free;                          /* 1 = livre (na free-list) */
    struct spud_hdr *next;                /* proximo bloco livre na free-list */
} spud_hdr_t;
static spud_hdr_t *spud_fl = 0;
static uintptr_t spud_heap_va = 0, spud_heap_end = 0;

static spud_hdr_t *spud_blk_next(spud_hdr_t *h){
    return (spud_hdr_t*)((char*)h + SPUD_HDRSZ + h->size + SPUD_RBLK);
}
static void spud_set_footer(spud_hdr_t *h){
    *(size_t*)((char*)h + SPUD_HDRSZ + h->size) = h->size;
}
static void spud_grow(size_t want){
    size_t pages=(want+0xFFF)/0x1000;
    if(pages<16) pages=16;
    long va=spud_sys(SYS_MMAP, (uint64_t)pages, 0, 0, 0, 0);
    if(va<=0) return;
    if(!spud_heap_va) spud_heap_va=(uintptr_t)va;
    spud_heap_end=(uintptr_t)va + pages*0x1000;
    spud_hdr_t *h=(spud_hdr_t*)(uintptr_t)va;
    h->size=pages*0x1000 - SPUD_HDRSZ - SPUD_RBLK;
    h->free=1;
    h->next=spud_fl;
    spud_set_footer(h);
    spud_fl=h;
}
static void *spud_alloc_locked(size_t need){
    spud_hdr_t **pp=&spud_fl;
    while(*pp){
        spud_hdr_t *h=*pp;
        if(h->size>=need){
            if(h->size >= need + SPUD_HDRSZ + SPUD_RBLK + 16){  /* sobra: divide */
                char *after=(char*)h + SPUD_HDRSZ + need;
                spud_hdr_t *nk=(spud_hdr_t*)(after + SPUD_RBLK);
                nk->size=h->size-need-SPUD_HDRSZ-SPUD_RBLK;
                nk->free=1;
                nk->next=h->next;
                h->size=need;
                h->free=0;
                *(size_t*)after=need;      /* footer do bloco alocado */
                spud_set_footer(nk);
                *pp=nk;
                return (char*)h+SPUD_HDRSZ;
            }
            h->free=0;                     /* pega o bloco inteiro */
            *pp=h->next;
            return (char*)h+SPUD_HDRSZ;
        }
        pp=&h->next;
    }
    return 0;
}
static void *spud_malloc(size_t n){
    if(n==0) n=1;
    n=(n+15)&~((size_t)15);
    void *p=spud_alloc_locked(n);
    if(!p){
        spud_grow(n<SPUD_HEAP_CHUNK?SPUD_HEAP_CHUNK:n);
        p=spud_alloc_locked(n);
    }
    return p;
}
/* remove 'h' da free-list (se estiver nela) */
static void spud_unlink(spud_hdr_t *h){
    spud_hdr_t **pp=&spud_fl;
    while(*pp && *pp!=h) pp=&(*pp)->next;
    if(*pp) *pp=h->next;
}
/* funde blocos livres adjacentes em qualquer ordem de free */
static void spud_coalesce(spud_hdr_t *h){
    for(;;){                             /* funde com o proximo (loop) */
        spud_hdr_t *nx=spud_blk_next(h);
        if((uintptr_t)nx>=spud_heap_end) break;
        if(!nx->free) break;
        spud_unlink(nx);
        h->size += SPUD_HDRSZ + SPUD_RBLK + nx->size;
        spud_set_footer(h);
    }
    for(;;){                             /* funde com o anterior */
        if((uintptr_t)h<=spud_heap_va) break;
        size_t pf=*(size_t*)((char*)h-SPUD_RBLK);
        if(pf==0 || pf>(uintptr_t)h-(uintptr_t)spud_heap_va) break;
        spud_hdr_t *pr=(spud_hdr_t*)((char*)h-SPUD_RBLK-pf-SPUD_HDRSZ);
        if((uintptr_t)pr<spud_heap_va || !pr->free || pr->size!=pf) break;
        spud_unlink(h);
        pr->size += SPUD_HDRSZ + SPUD_RBLK + h->size;
        spud_set_footer(pr);
        h=pr;
    }
}
static void spud_free(void *ptr){
    if(!ptr) return;
    spud_hdr_t *h=(spud_hdr_t*)((char*)ptr-SPUD_HDRSZ);
    h->free=1;
    h->next=spud_fl;
    spud_fl=h;
    spud_coalesce(h);
}

#endif /* SPUD_H */