/* synd.c — servico 'synd' (ring 3, ABI 1.6). Espelho EXATO do fsd/authd:
   o synd e o DONO do store userspace de contratos do synallagma (copia
   userspace no heap do servico). O kernel fica como PRIMITIVO/FALLBACK
   (SYS_CONTRACT_LIST + primitivo kernel mantem; o synd usa svc-first com
   fallback syscall — modelo authd/fsd provado). Fecha STATUS.md:183
   ("Synallagma ... politica em kernel, nao servico" -> agora e servico).
   Trilho de auditoria userspace no heap (espelho fsd AUDIT). 12o marker:
   "[synd] store ring3: ..." (runtest 12 markers x 5 boots). */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

/* ---- trilho de auditoria do servico (espelho fsd AUDIT) ---- */
#define SYND_AUD_MAX 16
#define SYND_AUD_PATH 44
static char synd_aud_op[SYND_AUD_MAX];
static uint32_t synd_aud_from[SYND_AUD_MAX];
static char synd_aud_path[SYND_AUD_MAX][SYND_AUD_PATH];
static int synd_aud_n=0;

static void synd_aud_push(char op, uint32_t from, const char *path){
    int i=(synd_aud_n<SYND_AUD_MAX)?synd_aud_n:(synd_aud_n%SYND_AUD_MAX);
    if(synd_aud_n<SYND_AUD_MAX) synd_aud_n++;
    synd_aud_op[i]=op;
    synd_aud_from[i]=from;
    if(path) spud_ncpy(synd_aud_path[i], path, SYND_AUD_PATH);
    else synd_aud_path[i][0]=0;
}

/* ---- store do servico: contratos userspace (espelho fsd st_files) ---- */
#define SYND_MAX 16
#define SYND_PATH 44
typedef struct {
    char caminho[SYND_PATH];    /* caminho que o contrato protege */
    char nome[64];              /* nome do contrato (do arquivo) */
    int leitura, escrita, execucao;  /* permissoes do contrato */
    int voluntario, revogavel;       /* flags do contrato */
    int ativo, consentido;           /* estado da sessao */
} synd_ct_t;
static synd_ct_t synd_ct[SYND_MAX];
static int synd_ct_n=0;

static int synd_find(const char *path){
    for(int i=0;i<synd_ct_n;i++){
        if(spud_cmp(synd_ct[i].caminho, path?path:"")==0) return i;
    }
    return -1;
}

static int synd_find_nome(const char *nome){
    for(int i=0;i<synd_ct_n;i++){
        if(spud_cmp(synd_ct[i].nome, nome?nome:"")==0) return i;
    }
    return -1;
}

static void synd_st_remove(int i){
    if(i>=0 && i<synd_ct_n){
        if(i<synd_ct_n-1) synd_ct[i]=synd_ct[synd_ct_n-1];
        synd_ct_n--;
    }
}

/* semente: tras os contratos VIGENTES do primitivo kernel (SYS_CONTRACT_LIST)
   para o store userspace do servico — o synd passa a ser o DONO da politica;
   o kernel aplica/fallback. (Espelho exato do fsd_st_seed com a lista de
   contratos em vez dos arquivos.) GOTCHA (mesmo do SYS_FS_LIST): o retorno
   do syscall e 0 em sucesso (conteudo no buffer, nao e tamanho) — a leitura
   varre ate o NUL. Formato por linha (kernel): "  - <nome> em <caminho>
   (r=%d w=%d x=%d)\n"; o nome pode conter espacos. */
static void synd_st_seed(void){
    char list[4096];
    spud_sys(SYS_CONTRACT_LIST, (uint64_t)(uintptr_t)list,
             (uint64_t)sizeof(list)-1, 0, 0, 0);
    size_t L=0; while(L+1<sizeof(list) && list[L]) L++;
    list[L]=0;
    char *p=list;
    while(*p && synd_ct_n<SYND_MAX){
        while(*p==' '||*p=='\t') p++;
        if(*p!='-'){ while(*p && *p!='\n') p++; if(*p) p++; continue; }
        p++;
        while(*p==' ') p++;
        const char *em=spud_strstr(p," em ");
        const char *rp=em? spud_strstr(em+4," (r=") : 0;
        if(!em || !rp){ while(*p && *p!='\n') p++; if(*p) p++; continue; }
        char nome[64]; size_t nn=(size_t)(em-p); if(nn>63) nn=63;
        for(size_t k=0;k<nn;k++) nome[k]=p[k]; nome[nn]=0;
        char caminho[SYND_PATH];
        size_t cn=(size_t)(rp-(em+4)); if(cn>SYND_PATH-1) cn=SYND_PATH-1;
        for(size_t k=0;k<cn;k++) caminho[k]=(em+4)[k]; caminho[cn]=0;
        if(!caminho[0]){ while(*p && *p!='\n') p++; if(*p) p++; continue; }
        int lr=1,wr=1,xr=1;
        const char *d=rp+4;               /* depois de " (r=" */
        lr=(*d=='1')?1:0; if(*d) d++;
        if(*d==' ') d++;
        if(*d=='w' && d[1]=='='){ d+=2; wr=(*d=='1')?1:0; if(*d) d++; }
        if(*d==' ') d++;
        if(*d=='x' && d[1]=='='){ d+=2; xr=(*d=='1')?1:0; }
        spud_ncpy(synd_ct[synd_ct_n].nome, nome, sizeof(synd_ct[0].nome));
        spud_ncpy(synd_ct[synd_ct_n].caminho, caminho, sizeof(synd_ct[0].caminho));
        synd_ct[synd_ct_n].leitura=lr;
        synd_ct[synd_ct_n].escrita=wr;
        synd_ct[synd_ct_n].execucao=xr;
        synd_ct[synd_ct_n].voluntario=1;
        synd_ct[synd_ct_n].revogavel=1;
        synd_ct[synd_ct_n].ativo=1;
        synd_ct[synd_ct_n].consentido=0;
        synd_ct_n++;
        while(*p && *p!='\n') p++;
        if(*p) p++;
    }
    /* marker de store userspace (12o marker do runtest) — INCONDICIONAL,
       espelho exato do fsd (que printa mesmo com store vazio): o marker
       PROVA que o seed rodou e a contagem REAL de contratos userspace. */
    spud_write("[synd] store ring3: ");
    {
        char nb[16]; int nd=0; int v=synd_ct_n;
        if(v==0){ nb[0]='0'; nd=1; }
        while(v>0 && nd<15){ nb[nd++]=(char)('0'+(v%10)); v/=10; }
        while(nd){ spud_write_ch(nb[--nd]); }
    }
    spud_write(" contratos userspace (dono da politica: kernel=fallback)\n");
}

/* ---- handler IPC do servico (espelho fsd_handler/authd_handler) ---- */
static int synd_handler(uint32_t from, uint32_t type, const char *req,
                        char *resp, size_t resp_max){
    if(type==IPC_TYPE_PING){
        spud_ncpy(resp, "pong", resp_max);
        return 4;
    }
    if(type==IPC_TYPE_SYN_LIST){
        /* req = o que filtrar (vazio = todos); resp = trilho textual dos
           contratos vigentes com estado de consentimento na sessao. */
        char filtro[48]; spud_ncpy(filtro, req?req:"", sizeof(filtro));
        synd_aud_push('l', from, filtro);
        size_t used=0;
        for(int i=0;i<synd_ct_n && used+1<resp_max;i++){
            char pre[4]; pre[0]=' '; pre[1]=' '; pre[2]=' '; pre[3]=0;
            if(!filtro[0] || spud_strstr(synd_ct[i].nome, filtro)
               || spud_strstr(synd_ct[i].caminho, filtro)){
                const char *cmp[2]={ synd_ct[i].leitura?"r":"-",
                                     synd_ct[i].escrita?"w":"-" };
                const char *flg[2]={ synd_ct[i].voluntario?"v":"-",
                                     synd_ct[i].revogavel?"g":"-" };
                resp[used++]=pre[0]; resp[used++]=pre[1];
                for(int k=0; synd_ct[i].nome[k] && used+1<resp_max; k++)
                    resp[used++]=synd_ct[i].nome[k];
                resp[used++]=' ';
                resp[used++]=cmp[0][0]; resp[used++]=cmp[1][0];
                resp[used++]=' ';
                resp[used++]=flg[0][0]; resp[used++]=(synd_ct[i].ativo?'a':'-');
                resp[used++]=' ';
                resp[used++]=(synd_ct[i].consentido?'1':'0');
                resp[used++]='\n';
            }
        }
        resp[used]=0;
        return (int)used;
    }
    if(type==IPC_TYPE_SYN_READ){
        /* req = caminho -> resp = campos do contrato '  nome l w x v g a c'
           (mesmo formato do list, contrato unico) ou "0" se nao existe. */
        int i=synd_find(req?req:"");
        if(i<0){ spud_ncpy(resp, "0", resp_max); return 1; }
        synd_aud_push('r', from, req?req:"");
        resp[0]=' '; resp[1]=' ';
        size_t used=2;
        for(int k=0; synd_ct[i].nome[k] && used+1<resp_max; k++)
            resp[used++]=synd_ct[i].nome[k];
        resp[used++]=' ';
        resp[used++]=synd_ct[i].leitura?'r':'-';
        resp[used++]=synd_ct[i].escrita?'w':'-';
        resp[used++]=' ';
        resp[used++]=synd_ct[i].execucao?'x':'-';
        resp[used++]=synd_ct[i].voluntario?'v':'-';
        resp[used++]=synd_ct[i].revogavel?'g':'-';
        resp[used++]=' ';
        resp[used++]=(synd_ct[i].ativo?'a':'-');
        resp[used++]=(synd_ct[i].consentido?'1':'0');
        resp[used+1]=0;
        return (int)(used+1);
    }
    if(type==IPC_TYPE_SYN_CONSENT){
        /* req = caminho -> conceder consentimento voluntario p/ a sessao
           corrente (a sessao ja consentiu na tela de login; o synd registra
           e o kernel aplica a regra na sessao = ABI "enforcement aplica a
           regra recebida"). resp='1'/'0'. */
        int i=synd_find(req?req:"");
        if(i<0){ spud_ncpy(resp, "0", resp_max); return 1; }
        synd_aud_push('c', from, req?req:"");
        synd_ct[i].consentido=1;
        spud_ncpy(resp, "1", resp_max);
        return 1;
    }
    if(type==IPC_TYPE_SYN_REVOKE){
        int i=synd_find(req?req:"");
        if(i<0){ spud_ncpy(resp, "0", resp_max); return 1; }
        synd_aud_push('r', from, req?req:"");
        synd_ct[i].consentido=0;
        spud_ncpy(resp, "1", resp_max);
        return 1;
    }
    if(type==IPC_TYPE_SYN_AUDIT){
        /* resp = trilho de auditoria do servico (mais recente primeiro),
           espelho IPC_TYPE_FS_AUDIT do fsd. */
        size_t used=0;
        size_t n=synd_aud_n;
        for(size_t k=0;k<n && used+2+SYND_AUD_PATH<resp_max;k++){
            size_t i=(n-1-k)%SYND_AUD_MAX;
            (void)i;
            resp[used++]=synd_aud_op[i]?synd_aud_op[i]:' ';
            resp[used++]=' ';
            for(int j=0; synd_aud_path[i][j] && used+1<resp_max; j++)
                resp[used++]=synd_aud_path[i][j];
            resp[used++]='\n';
        }
        resp[used]=0;
        return (int)used;
    }
    spud_ncpy(resp, "?", resp_max);
    return 1;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    synd_st_seed();
    spud_write("[synd] servico 'synd' ring 3: store userspace de contratos "
               "(ABI 1.6, svc-first + fallback syscall)\n");
    spud_svc_serve("synd", synd_handler);
    /* so sai daqui se o registro falhou */
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 0;
}
