/* authd — servico de autenticacao RING 3 (ABI 1.5, MARCO microkernel).
   Dono do store de credenciais (nome/salt/hash/caps) e da POLITICA: se ha
   conta, se a senha confere (com custo deliberado), quem e o soberano, quem
   pode criar/remover/trocar. O hash PBKDF2-HMAC-SHA256 roda como PRIMITIVA
   COMPUTACIONAL do kernel (SYS_AUTH_HASH, IRQs off) — o motivo e um bug real:
   em ring 3, computo longo preemptado por IRQ corrompe registradores/
   memoria de forma NAO-DETERMINISTICA (mesma classe do bug fix 2/3; o kernel
   usa disable_interrupts() ao redor do PBKDF2 no login classico). O authd
   executa o hash via syscall e mantem o restante (store+regra+ticket) em
   ring 3, como SERVICES.md 3.2: kernel aplica uma regra recebida do servico.

   Registra o servico "auth" e atende IPC. Apos validar nome+senha, emite um
   TICKET de sessao no kernel via SYS_AUTH_TICKET_ISSUE (CAP_USER_ADMIN);
   SYS_SESSION_LOGIN(a3=tok) consome o ticket e monta a sessao com os caps
   decididos aqui. Sem o authd registrado, os apps caem nos syscalls SYS_AUTH_*
   (store do kernel) — um fluxo unico por boot. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

/* ---- constantes (espelham thais.h) ---- */
#define AUTH_MAX     16
#define AUTH_NAME    32
#define AUTH_HASH    65           /* 64 hex + NUL */
#define AUTH_SALT    33           /* 32 hex + NUL */
#define CAP_FB_DRAW       (1ULL<<0)
#define CAP_AISTHESIS     (1ULL<<1)
#define CAP_IDIOS_WRITE   (1ULL<<5)
#define CAP_FS_READ       (1ULL<<6)
#define CAP_API_ALL       0xFFFFFFFFFFFFFFFFULL
#define CAP_USER_DEFAULT  (CAP_FB_DRAW|CAP_AISTHESIS|CAP_FS_READ|CAP_IDIOS_WRITE)

typedef struct {
    char     name[AUTH_NAME];
    char     hash[AUTH_HASH];
    char     salt[AUTH_SALT];
    uint64_t caps;
    int      active;
    int      is_admin;
} acct_t;

static acct_t accts[AUTH_MAX];
static int acct_n=0;
static int admin_authed=0;        /* admin validou credenciais neste boot */

/* ---- utilitarios ---- */
static size_t alen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
static int acmp(const char *a, const char *b){ while(*a && *a==*b){ a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static void ahex(char *out, const uint8_t *b, size_t n){
    static const char *dig="0123456789abcdef";
    for(size_t i=0;i<n;i++){ out[i*2]=dig[b[i]>>4]; out[i*2+1]=dig[b[i]&15]; }
    out[n*2]=0;
}
static void dec_str(char *out, long v){
    char tmp[16]; int n=0;
    if(v==0){ out[0]='0'; out[1]=0; return; }
    while(v>0 && n<(int)sizeof(tmp)-1){ tmp[n++]=(char)('0'+(v%10)); v/=10; }
    int k=0; while(n) out[k++]=tmp[--n]; out[k]=0;
}

/* RNG do salt: entropia do kernel via uptime/pid + xorshift. */
static uint64_t rng_state=0x243F6A8885A308D3ULL;
static uint64_t rng(void){
    uint64_t x=rng_state ^ ((uint64_t)spud_sys(SYS_UPTIME,0,0,0,0,0)<<3)
                           ^ ((uint64_t)spud_sys(SYS_GETPID,0,0,0,0,0)<<17)
                           ^ ((uint64_t)spud_sys(SYS_GETPPID,0,0,0,0,0)<<41);
    x ^= x<<13; x ^= x>>7; x ^= x<<17;
    rng_state = x ^ 0x9E3779B97F4A7C15ULL;
    return rng_state;
}

static void gen_salt(char out[AUTH_SALT]){
    uint64_t r1=rng(), r2=rng();
    for(int i=0;i<8;i++){
        int v=(int)((r1>>(i*7))&0x3f);
        out[i*2]  ="0123456789abcdef"[v&15];
        out[i*2+1]="0123456789abcdef"[(v>>3)&15];
    }
    for(int i=0;i<8;i++){
        int v=(int)((r2>>(i*7))&0x3f);
        out[16+i*2]  ="0123456789abcdef"[v&15];
        out[16+i*2+1]="0123456789abcdef"[(v>>3)&15];
    }
    out[32]=0;
}

/* hash via primitiva computacional do kernel (SYS_AUTH_HASH, IRQs off).
   pw e truncado a 63 bytes (limite old kernel); salt e texto hex de 32 chars. */
static void authd_hash(const char *password, const char *salt, char out_hex[AUTH_HASH]){
    char pw[64]; size_t i=0;
    while(password && password[i] && i<63){ pw[i]=password[i]; i++; }
    pw[i]=0;
    long r=spud_sys(SYS_AUTH_HASH,
        (uint64_t)(uintptr_t)pw,
        (uint64_t)(uintptr_t)salt,
        (uint64_t)(uintptr_t)out_hex, 0, 0);
    if(r!=0) out_hex[0]=0;
}

/* ---- store ---- */
static acct_t* find(const char *name){
    for(int i=0;i<acct_n;i++) if(accts[i].active && acmp(accts[i].name,name)==0) return &accts[i];
    return 0;
}
static acct_t* create(const char *name, const char *password, uint64_t caps, int admin){
    if(acct_n>=AUTH_MAX) return 0;
    if(find(name)) return 0;
    acct_t *u=&accts[acct_n++];
    spud_ncpy(u->name, name, sizeof(u->name));
    u->caps=caps; u->active=1; u->is_admin=admin;
    gen_salt(u->salt);
    authd_hash(password, u->salt, u->hash);
    if(!u->hash[0]){ u->active=0; acct_n--; return 0; }
    return u;
}

static int verify(const char *name, const char *password){
    acct_t *u=find(name);
    if(!u || !u->active) return 0;
    char test[AUTH_HASH]; authd_hash(password, u->salt, test);
    return test[0] && acmp(test,u->hash)==0;
}

static int change_password(const char *name, const char *oldpw, const char *newpw){
    if(alen(newpw)<4) return 0;
    acct_t *u=find(name);
    if(!u || !verify(name,oldpw)) return 0;
    gen_salt(u->salt);
    authd_hash(newpw,u->salt,u->hash);
    return u->hash[0] ? 1 : 0;
}

/* ---- handler IPC ---- */
static int authd_handler(uint32_t from, uint32_t type,
                         const char *req, char *resp, size_t resp_max){
    (void)from;
    if(type==IPC_TYPE_PING){
        spud_ncpy(resp, "pong", resp_max); return 4;
    }
    if(type==IPC_TYPE_AUTH_COUNT){
        dec_str(resp, acct_n);
        return (int)alen(resp);
    }
    if(type==IPC_TYPE_AUTH_LIST){
        size_t used=0;
        if(acct_n==0){ spud_ncpy(resp,"(nenhum usuario)\n",resp_max); return 17; }
        for(int i=0;i<acct_n;i++){
            if(!accts[i].active) continue;
            char line[96]; line[0]=0;
            size_t o=0;
            { const char *p="  "; while(*p && o<sizeof(line)-1) line[o++]=*p++; }
            { const char *p=accts[i].name; while(*p && o<sizeof(line)-1) line[o++]=*p++; }
            { const char *p=accts[i].is_admin?" (soberano)\n":" (voluntario)\n"; while(*p && o<sizeof(line)-1) line[o++]=*p++; }
            line[o]=0;
            if(used+o<resp_max){ for(size_t k=0;k<o;k++) resp[used++]=line[k]; }
        }
        resp[used]=0;
        return (int)used;
    }
    /* pedidos com campos NUL-separados: [f1]\0[f2]\0[f3]\0 */
    char f1[AUTH_NAME], f2[64], f3[64];
    f1[0]=f2[0]=f3[0]=0;
    {
        const char *p=req; size_t i=0;
        while(*p && i<sizeof(f1)-1) f1[i++]=*p++;
        f1[i]=0;
        if(*p) p++;
        i=0; while(*p && i<sizeof(f2)-1) f2[i++]=*p++;
        f2[i]=0;
        if(*p) p++;
        i=0; while(*p && i<sizeof(f3)-1) f3[i++]=*p++;
        f3[i]=0;
    }
    if(type==IPC_TYPE_AUTH_CREATE){
        /* bootstrap: so o 1o usuario (soberano, CAP_ALL) */
        if(acct_n!=0){ spud_ncpy(resp,"0",resp_max); return 1; }
        if(alen(f1)<3 || alen(f2)<4){ spud_ncpy(resp,"0",resp_max); return 1; }
        resp[0] = create(f1,f2,CAP_API_ALL,1) ? '1' : '0';
        resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_AUTH_LOGIN){
        /* verifica credenciais e, se ok, emite ticket de sessao no kernel */
        if(!verify(f1,f2)){ spud_ncpy(resp,"0",resp_max); return 1; }
        acct_t *u=find(f1);
        if(!u){ spud_ncpy(resp,"0",resp_max); return 1; }
        uint64_t tok=0;
        long ir=spud_sys(SYS_AUTH_TICKET_ISSUE,
            (uint64_t)(uintptr_t)u->name, u->caps, (uint64_t)(uintptr_t)&tok, 0, 0);
        if(ir==0 && tok){
            if(u->is_admin) admin_authed=1;
            char hex[17];
            for(int i=0;i<16;i++){ int sh=(60-i*4); int d=(int)((tok>>sh)&15ULL); hex[i]="0123456789abcdef"[d]; }
            hex[16]=0;
            spud_ncpy(resp,hex,resp_max);
            return 16;
        }
        spud_ncpy(resp,"0",resp_max);
        return 1;
    }
    if(type==IPC_TYPE_AUTH_DELETE){
        /* admin: remove usuario (nunca o soberano; precisa sessao soberana) */
        acct_t *u=find(f1);
        if(!u || u->is_admin || !admin_authed){ spud_ncpy(resp,"0",resp_max); return 1; }
        u->active=0;
        resp[0]='1'; resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_AUTH_CHANGEPW){
        /* self troca com a senha atual; admin troca de quem quiser */
        if(alen(f3)<4){ spud_ncpy(resp,"0",resp_max); return 1; }
        if(change_password(f1,f2,f3)){ resp[0]='1'; resp[1]=0; return 1; }
        acct_t *u=find(f1);
        if(u && admin_authed){
            gen_salt(u->salt);
            authd_hash(f3,u->salt,u->hash);
            if(u->hash[0]){ resp[0]='1'; resp[1]=0; return 1; }
        }
        spud_ncpy(resp,"0",resp_max);
        return 1;
    }
    if(type==IPC_TYPE_AUTH_CREATE_USER){
        if(!admin_authed){ spud_ncpy(resp,"0",resp_max); return 1; }
        if(alen(f1)<3 || alen(f2)<4){ spud_ncpy(resp,"0",resp_max); return 1; }
        resp[0] = create(f1,f2,CAP_USER_DEFAULT,0) ? '1' : '0';
        resp[1]=0;
        return 1;
    }
    spud_ncpy(resp,"?",resp_max);
    return 1;
}

/* ---- self-test: hash via syscall compara com vetor conhecido ----
   (pbkdf2_hmac de Python, P="password", salt 32-hex = b"salt"*4, c=100000) */
static void selftest(void){
    char hex[AUTH_HASH];
    authd_hash("password", "73616c7473616c7473616c7473616c74", hex);
    static const char *expect="4fbf2d122fe6afc61a81e9f2fe393ab39f906a78ddddc797763c0e784857e9b4";
    if(!hex[0]){ spud_write("[authd] PBKDF2 syscall FALHOU (ret)\n"); return; }
    if(acmp(hex,expect)==0){
        spud_write("[authd] PBKDF2 self-test ok\n");
    } else {
        spud_write("[authd] PBKDF2 syscall FALHOU vetor: ");
        spud_write(hex);
        spud_write("\n");
    }
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    selftest();
    spud_write("[authd] servico 'auth' ring 3: store+politica+ticket (ABI 1.5), hash via kernel\n");
    spud_svc_serve("auth", authd_handler);
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 0;
}