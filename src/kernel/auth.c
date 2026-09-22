#include "thais.h"
#include <stdbool.h>

/* auth.c — autenticacao de usuarios (Auditoria Auth).
   Substituiu o hashing FNV-1a (nao-cryptografico) por PBKDF2-HMAC-SHA256:
   hash = 32 bytes (64 hex) com salt de 16 bytes e AUTH_HASH_ITER iteracoes.
   Apenas o HASH e armazenado (nunca a senha). ACHOU um KDF real, com custo
   deliberado, adequado a senhas de baixa entropia (o que FNV-1a NUNCA foi). */

static auth_user_t accounts[AUTH_USER_MAX];
static int account_count=0;
static auth_user_t *current_user=0;

void pbkdf2_hmac_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen,
                        uint32_t iters, uint8_t *out, size_t outlen);

static void gen_salt(char out[AUTH_SALT_SIZE]){
    /* 16 bytes aleatorios representados como 32 hex chars, NUL-terminated */
    uint64_t r1=rand64(), r2=rand64();
    snprintf(out, AUTH_SALT_SIZE, "%016llx%016llx", (unsigned long long)r1, (unsigned long long)r2);
}

/* chave do PBKDF2: a propria senha em bytes UTF-8 (as criptografias de texto
   aceitam bytes crus; o kernel limita cada password a 63 bytes no maximo). */
static void hex_to_bytes(const char *hex, uint8_t *out){
    for(size_t i=0; hex[i] && hex[i+1]; i+=2){
        uint8_t hi=hex[i], lo=hex[i+1];
        uint8_t v=0;
        if     (hi>='0'&&hi<='9') v=(uint8_t)(hi-'0');
        else if(hi>='a'&&hi<='f') v=(uint8_t)(hi-'a'+10);
        else if(hi>='A'&&hi<='F') v=(uint8_t)(hi-'A'+10);
        v<<=4;
        uint8_t lv=0;
        if     (lo>='0'&&lo<='9') lv=(uint8_t)(lo-'0');
        else if(lo>='a'&&lo<='f') lv=(uint8_t)(lo-'a'+10);
        else if(lo>='A'&&lo<='F') lv=(uint8_t)(lo-'A'+10);
        out[i/2]=(uint8_t)(v|lv);
    }
}

void auth_hash_password(const char *password, const char *salt, char out_hex[65]){
    uint8_t salt_bytes[16];
    hex_to_bytes(salt, salt_bytes);
    /* FiX: hex_to_bytes devolve TODOS os 16 bytes; com salt curto, bytes
       residuais eram stack-lixo e entravam no PBKDF2 (salt eff. != salt).
       Contas reais usam sempre 32 hex; aqui calcula-se o tamanho real para
       qualquer salt, e o selftest do authd valida contra vetor de Python. */
    size_t sl=0;
    if(salt){ while(salt[sl]) sl++; sl/=2; if(sl>16) sl=16; }
    const char *pw = password ? password : "";
    size_t pl = 0;
    while(pw[pl] && pl < 63) pl++;
    uint8_t dk[32];
    /* FiX: PBKDF2 e caro (~5ms) e roda como processo. Com IRQs/preempcao
       ativas, o ISR do PIT sobre o kernel stack corrompia o estado durante o
       hash (-> #PF em sha256_*). Ela roda critica, com IRQs desligadas; o
       scheduler espera ~5ms. Root cause do ISR ainda sob investigacao. */
    disable_interrupts();
    pbkdf2_hmac_sha256((const uint8_t*)pw, pl, salt_bytes, sl, AUTH_HASH_ITER, dk, sizeof(dk));
    enable_interrupts();
    for(int i=0;i<32;i++) snprintf(out_hex+i*2, 3, "%02x", dk[i]);
    out_hex[64]=0;
}

void auth_init(void){
    for(int i=0;i<AUTH_USER_MAX;i++) accounts[i].active=false;
    account_count=0;
    kprint("[nomos] auth: aguardando bootstrap de usuario\n");
}

bool auth_create_user(const char *name, const char *password, uint64_t caps, bool admin){
    if(account_count>=AUTH_USER_MAX) return false;
    for(int i=0;i<account_count;i++) if(strcmp(accounts[i].name,name)==0) return false;
    auth_user_t *u=&accounts[account_count++];
    strncpy(u->name,name,AUTH_NAME_MAX);
    u->name[AUTH_NAME_MAX-1]=0;
    char salt[AUTH_SALT_SIZE]; gen_salt(salt);
    strncpy(u->salt,salt,AUTH_SALT_SIZE);
    u->salt[AUTH_SALT_SIZE-1]=0;
    auth_hash_password(password,salt,u->hash);
    u->caps=caps; u->active=true; u->is_admin=admin;
    return true;
}

bool auth_verify(const char *name, const char *password){
    auth_user_t *u=auth_find(name);
    if(!u || !u->active) return false;
    char test[AUTH_HASH_SIZE]; auth_hash_password(password,u->salt,test);
    return strcmp(test,u->hash)==0;
}

auth_user_t* auth_find(const char *name){
    for(int i=0;i<account_count;i++) if(accounts[i].active && strcmp(accounts[i].name,name)==0) return &accounts[i];
    return 0;
}

void auth_list(void){
    if(account_count==0){ serial_write("  (nenhum usuario)\n"); fb_console_write("  (nenhum usuario)\n"); return; }
    char b[64];
    for(int i=0;i<account_count;i++) if(accounts[i].active){
        snprintf(b,64,"  %s %s caps=%llx\n", accounts[i].name, accounts[i].is_admin?"(soberano)":"(voluntario)", (unsigned long long)accounts[i].caps);
        serial_write(b); fb_console_write(b);
    }
}

bool auth_change_password(const char *name, const char *oldpw, const char *newpw){
    auth_user_t *u=auth_find(name);
    if(!u) return false;
    if(!auth_verify(name,oldpw)) return false;
    char salt[AUTH_SALT_SIZE]; gen_salt(salt);
    strncpy(u->salt,salt,AUTH_SALT_SIZE);
    u->salt[AUTH_SALT_SIZE-1]=0;
    auth_hash_password(newpw,salt,u->hash);
    return true;
}

/* admin-only: troca senha sem conhecer a anterior */
bool auth_change_password_admin(const char *name, const char *newpw){
    auth_user_t *u=auth_find(name);
    if(!u) return false;
    char salt[AUTH_SALT_SIZE]; gen_salt(salt);
    strncpy(u->salt,salt,AUTH_SALT_SIZE);
    u->salt[AUTH_SALT_SIZE-1]=0;
    auth_hash_password(newpw,salt,u->hash);
    return true;
}

/* desativa usuario (soft delete: nao reutiliza slot) */
bool auth_user_delete(const char *name){
    auth_user_t *u=auth_find(name);
    if(!u) return false;
    u->active=false;
    return true;
}

/* preenche buf com lista textual de usuarios; retorna bytes escritos */
int auth_list_text(char *buf, size_t max){
    size_t used=0;
    #define A(s) do{ size_t l=strlen(s); if(used+l<max){ memcpy(buf+used,(s),l); used+=l; } }while(0)
    if(account_count==0){ A("(nenhum usuario)\n"); buf[used]=0; return (int)used; }
    for(int i=0;i<account_count;i++) if(accounts[i].active){
        char line[96];
        snprintf(line,sizeof(line),"  %s %s caps=%llx\n",
            accounts[i].name,
            accounts[i].is_admin?"(soberano)":"(voluntario)",
            (unsigned long long)accounts[i].caps);
        A(line);
    }
    #undef A
    if(used<max) buf[used]=0;
    return (int)used;
}

auth_user_t* login_result_user(void){ return current_user; }
void login_set_user(auth_user_t *u){ current_user=u; }
int auth_user_count(void){ return account_count; }

/* ---- ABI 1.5: tickets de sessao (servico 'auth' em ring 3) ----
   O store de credenciais migra para o authd (userspace). O kernel nao guarda
   contas nesse fluxo: guarda TICKETS one-shot emitidos pelo authd apos ele
   verificar nome+senha. Enforcement do login continua no kernel, mas com a
   regra (caps) recebida do servico — evita TOCTOU e mantem o kernel como
   unico concedente de capacidades/estado de sessao. */
#define AUTH_TICKET_MAX 16
typedef struct {
    char     name[AUTH_NAME_MAX];
    uint64_t caps;
    uint64_t tok;
    bool     used;
} auth_ticket_t;

static auth_ticket_t tickets[AUTH_TICKET_MAX];
static int ticket_count=0;
static uint64_t ticket_seq=0;
static auth_user_t synth_user;          /* "conta" sintetica da sessao via ticket */

static uint64_t auth_ticket_token(void){
    ticket_seq++;
    uint64_t s=(uint64_t)ticks<<17 ^ ticket_seq*0x9E3779B97F4A7C15ULL;
    if(current_proc) s ^= (uint64_t)(uintptr_t)current_proc * 0x5851F42D4C957F2DULL;
    return s ^ ((uint64_t)(uint32_t)pit_latch_read()<<33);
}

bool auth_ticket_issue(const char *name, uint64_t caps, uint64_t *tok_out){
    if(!name || !name[0]) return false;
    if(ticket_count>=AUTH_TICKET_MAX) return false;
    for(int i=0;i<ticket_count;i++)           /* uma sessao por conta de cada vez */
        if(!tickets[i].used && strcmp(tickets[i].name,name)==0) tickets[i].used=true;
    auth_ticket_t *t=&tickets[ticket_count++];
    strncpy(t->name,name,AUTH_NAME_MAX); t->name[AUTH_NAME_MAX-1]=0;
    t->caps=caps; t->tok=auth_ticket_token(); t->used=false;
    if(tok_out) *tok_out=t->tok;
    return true;
}

auth_user_t* auth_ticket_consume(const char *name, uint64_t tok){
    for(int i=0;i<ticket_count;i++){
        auth_ticket_t *t=&tickets[i];
        if(t->used || strcmp(t->name,name)!=0 || t->tok!=tok) continue;
        t->used=true;
        strncpy(synth_user.name,name,AUTH_NAME_MAX); synth_user.name[AUTH_NAME_MAX-1]=0;
        synth_user.hash[0]=0; synth_user.salt[0]=0;
        synth_user.caps=t->caps; synth_user.active=true;
        synth_user.is_admin=(t->caps==CAP_ALL);
        return &synth_user;
    }
    return 0;
}

int auth_user_index_current(void){
    if(!current_user) return 0xFF;
    for(int i=0;i<account_count;i++) if(current_user==&accounts[i]) return i;
    return 0xFF;
}