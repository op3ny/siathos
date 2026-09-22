/* fsd — filesystem daemon (MARCO 9 + ABI 1.5). Servico userspace de arquivos.
   Registra "fsd" e atende IPC: READ/LIST/CREATE/WRITE/REMOVE/MKDIR + AUDIT
   (trilho de auditoria do servico). Modelo authd — o servico e o DONO do
   store userspace (/paradosis e /nomos vivem em COPY no heap do fsd); o
   kernel (SYS_FS_*) e o primitivo de storage usado como FALLBACK nos demais
   paths. Regra IPCA: kernel nunca bloqueia aguardando o servico; clientes
   usam IPC nao-bloqueante com retry+yield e fallback syscall. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

/* ---- trilho de auditoria do servico (valor proprio do fsd) ---- */
enum { AUDIT_MAX = 16, AUDIT_PATH = 44 };
static char audit_op[AUDIT_MAX];        /* 'r','l','c','w','d' */
static uint32_t audit_from[AUDIT_MAX];
static char audit_path[AUDIT_MAX][AUDIT_PATH];
static int audit_n=0;

static void audit_push(char op, uint32_t from, const char *path){
    int i = (audit_n < AUDIT_MAX) ? audit_n++ : (audit_n % AUDIT_MAX);
    audit_op[i]=op;
    audit_from[i]=from;
    if(path) spud_ncpy(audit_path[i], path, AUDIT_PATH);
    else audit_path[i][0]=0;
}

/* ---- store do servico (userspace): /paradosis e /nomos ---- */
enum { ST_MAX=16, ST_DATA=512, ST_DIRS=8 };
typedef struct { char path[48]; char data[ST_DATA]; size_t len; } st_file_t;
static st_file_t st_files[ST_MAX];
static int st_fn=0;
static char st_dirs[ST_DIRS][48];
static int st_dn=0;

static int fsd_st_find(const char *path){
    for(int i=0;i<st_fn;i++){
        if(spud_cmp(st_files[i].path, path)==0) return i;
    }
    return -1;
}

static int fsd_st_dir_find(const char *path){
    for(int i=0;i<st_dn;i++){
        if(spud_cmp(st_dirs[i], path)==0) return i;
    }
    return -1;
}

/* cria diretorio no espaco owned (registro em st_dirs; ja existe = ok) */
static int fsd_st_mkdir(const char *path){
    if(!path || !path[0]) return -1;
    if(fsd_st_dir_find(path)>=0) return 0;
    if(st_dn>=ST_DIRS) return -1;
    spud_ncpy(st_dirs[st_dn++], path, 48);
    return 0;
}

/* adiciona/sobrescreve arquivo local (write cria se nao existe, como o kernel) */
static int fsd_st_set(const char *path, const char *data, size_t len){
    int i=fsd_st_find(path);
    if(i<0){
        if(st_fn>=ST_MAX) return -1;
        i=st_fn++;
        spud_ncpy(st_files[i].path, path, sizeof(st_files[i].path));
    }
    if(len>ST_DATA-1) len=ST_DATA-1;
    for(size_t k=0;k<len;k++) st_files[i].data[k]=data[k];
    st_files[i].len=len;
    st_files[i].data[len]=0;
    return 0;
}

static int fsd_st_remove(const char *path){
    int i=fsd_st_find(path);
    if(i<0) return -1;
    if(i < st_fn-1){ st_files[i]=st_files[st_fn-1]; }
    st_fn--;
    return 0;
}

static int fsd_st_list(const char *dir, char *out, size_t max){
    size_t dl=spud_len(dir);
    size_t used=0;
    for(int i=0;i<st_fn && used<max;i++){
        const char *p=st_files[i].path;
        if(spud_cmp(p, dir)!=0 && spud_strstr(p, dir)!=p) continue;   /* precisa estar SOB dir/ */
        if(spud_cmp(p, dir)==0) continue;                             /* dir em si nao conta */
        if(p[dl]!='/') continue;
        if(spud_strstr(p+dl+1, "/")!=0) continue;                     /* so 1 nivel */
        /* "  <ARQ>   nome" — mesmo formato do fs_list do kernel */
        const char *nome=p+dl+1;
        size_t nl=spud_len(nome);
        if(used+10+nl<max){
            const char *t="  <ARQ>   ";
            while(*t && used+1<max) out[used++]=*t++;
            for(size_t k=0;k<nl && used+1<max;k++) out[used++]=nome[k];
            out[used++]='\n';
        }
    }
    /* dirs criados no espaco owned, no 1o nivel */
    for(int i=0;i<st_dn && used<max;i++){
        const char *p=st_dirs[i];
        if(spud_cmp(p, dir)!=0 && spud_strstr(p, dir)!=p) continue;
        if(spud_cmp(p, dir)==0) continue;
        if(p[dl]!='/') continue;
        if(spud_strstr(p+dl+1, "/")!=0) continue;
        const char *nome=p+dl+1;
        size_t nl=spud_len(nome);
        if(used+10+nl<max){
            const char *t="  <DIR>   ";
            while(*t && used+1<max) out[used++]=*t++;
            for(size_t k=0;k<nl && used+1<max;k++) out[used++]=nome[k];
            out[used++]='\n';
        }
    }
    out[used]=0;
    return (int)used;
}

/* paths sob a posse do servico: /paradosis e /nomos (dir em si ou descendentes) */
static int fsd_st_owns(const char *path){
    if(!path) return 0;
    if(spud_cmp(path,"/paradosis")==0 || spud_cmp(path,"/nomos")==0) return 1;
    if(spud_strstr(path,"/paradosis/")==path) return 1;
    if(spud_strstr(path,"/nomos/")==path) return 1;
    return 0;
}

/* semente: copia do kernel o que ja existe nos espacos que passam a ser
   do servico; dali em diante o fsd e a fonte de verdade userspace. */
static void fsd_st_seed(void){
    static const char *dirs[]={ "/bin","/kormi","/nomos","/paradosis",
                                "/idios","/oikos","/synallagma","/praxis" };
    for(size_t d=0;d<sizeof(dirs)/sizeof(dirs[0]) && st_dn<ST_DIRS;d++)
        spud_ncpy(st_dirs[st_dn++], dirs[d], 48);

    static const char *src[]={ "/nomos", "/paradosis" };
    for(size_t s=0;s<sizeof(src)/sizeof(src[0]);s++){
        char list[512];
        long lr=spud_sys(SYS_FS_LIST, (uint64_t)(uintptr_t)src[s],
                         (uint64_t)(uintptr_t)list, sizeof(list)-1, 0, 0);
        if(lr<0) continue;
        size_t L=0; while(L+1<sizeof(list) && list[L]) L++;
        list[L]=0;
        if(!L) continue;
        char *p=list;
        while(*p){
            while(*p==' ') p++;
            if(spud_strstr(p,"<ARQ>")!=p){ while(*p && *p!='\n') p++; if(*p) p++; continue; }
            p+=5;
            while(*p==' ') p++;
            char name[40]; size_t nl=0;
            while(*p && *p!='\n' && nl<sizeof(name)-1) name[nl++]=*p++;
            if(*p=='\n') p++;
            name[nl]=0;
            if(!nl) continue;
            char full[64]; spud_ncpy(full, src[s], sizeof(full));
            size_t fl=spud_len(full);
            if(fl+1+nl < sizeof(full)){
                full[fl]='/'; full[fl+1]=0;
                spud_ncat(full, name, sizeof(full));
                char data[ST_DATA];
                long got=spud_sys(SYS_FS_READ, (uint64_t)(uintptr_t)full,
                                  (uint64_t)(uintptr_t)data, ST_DATA-1, 0, 0);
                if(got>=0) fsd_st_set(full, data, (size_t)got);
            }
        }
    }
    spud_write("[fsd] store ring3: paradosis+nomos local no heap do servico (");
    {
        char nbuf[16]; int nd=0; int v=st_fn;
        if(v==0){ nbuf[0]='0'; nd=1; }
        while(v>0 && nd<15){ nbuf[nd++]=(char)('0'+v%10); v/=10; }
        while(nd) spud_write_ch(nbuf[--nd]);
    }
    spud_write(" arquivos)\n");
}

static int fsd_handler(uint32_t from, uint32_t type,
                       const char *req, char *resp, size_t resp_max){
    if(type==IPC_TYPE_PING){
        spud_ncpy(resp, "pong", resp_max);
        return 4;
    }
    if(type==IPC_TYPE_READ){
        if(!req || !req[0]){ audit_push('r', from, "?"); spud_ncpy(resp, "erro: path vazio", resp_max); return 16; }
        audit_push('r', from, req);
        if(fsd_st_owns(req)){
            int i=fsd_st_find(req);
            if(i<0){ spud_ncpy(resp, "erro: nao encontrado", resp_max); return 20; }
            for(size_t k=0;k<st_files[i].len && k+1<resp_max;k++) resp[k]=st_files[i].data[k];
            size_t got=st_files[i].len;
            resp[got]=0;
            return (int)got;
        }
        long r=spud_sys(SYS_FS_READ, (uint64_t)(uintptr_t)req,
                        (uint64_t)(uintptr_t)resp, resp_max-1, 0, 0);
        if(r<0){ spud_ncpy(resp, "erro: nao encontrado", resp_max); return 20; }
        size_t got=(size_t)r;
        resp[got]=0;
        return (int)got;
    }
    if(type==IPC_TYPE_LIST){
        if(!req || !req[0]){ audit_push('l', from, "?"); spud_ncpy(resp, "erro: dir invalido", resp_max); return 18; }
        audit_push('l', from, req);
        if(fsd_st_owns(req)){
            int n=fsd_st_list(req, resp, resp_max-1);
            if(n<0){ spud_ncpy(resp, "erro: dir invalido", resp_max); return 18; }
            return n;
        }
        long r=spud_sys(SYS_FS_LIST, (uint64_t)(uintptr_t)req,
                        (uint64_t)(uintptr_t)resp, resp_max-1, 0, 0);
        if(r<0){ spud_ncpy(resp, "erro: dir invalido", resp_max); return 18; }
        return (int)spud_len(resp);
    }
    if(type==IPC_TYPE_FS_CREATE){
        const char *path=req;
        char f[64]; spud_ncpy(f, path ? path : "", sizeof(f));
        audit_push('c', from, f);
        if(fsd_st_owns(f)){
            resp[0]=(fsd_st_set(f,"",0)==0)?'1':'0'; resp[1]=0;
            return 1;
        }
        long r=spud_sys(SYS_FS_CREATE, (uint64_t)(uintptr_t)f, 0, 0, 0, 0);
        resp[0]=(r==0)?'1':'0'; resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_FS_WRITE){
        /* req = "path\n<dados>" — sem NUL interno; dados = resto da string */
        if(!req || !req[0]){ audit_push('w', from, "?"); spud_ncpy(resp, "0", resp_max); return 1; }
        const char *nl=req;
        while(*nl && *nl!='\n') nl++;
        char path[64]; size_t pl=((size_t)(nl-req)<sizeof(path)-1)?(size_t)(nl-req):sizeof(path)-1;
        for(size_t i=0;i<pl;i++) path[i]=req[i];
        path[pl]=0;
        const char *data = *nl ? nl+1 : "";
        size_t dlen=spud_len(data);
        audit_push('w', from, path);
        if(fsd_st_owns(path)){
            resp[0]=(fsd_st_set(path, data, dlen)==0)?'1':'0'; resp[1]=0;
            return 1;
        }
        long r=spud_sys(SYS_FS_WRITE, (uint64_t)(uintptr_t)path,
                        (uint64_t)(uintptr_t)data, dlen, 0, 0);
        resp[0]=(r==0)?'1':'0'; resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_FS_REMOVE){
        const char *path=req;
        char f[64]; spud_ncpy(f, path ? path : "", sizeof(f));
        audit_push('d', from, f);
        if(fsd_st_owns(f)){
            if(fsd_st_remove(f)==0){ resp[0]='1'; resp[1]=0; return 1; }
            /* nao era arquivo: remove diretorio owned (raizes /paradosis,
               /nomos protegidas contra anairesis) */
            if(spud_cmp(f,"/paradosis")!=0 && spud_cmp(f,"/nomos")!=0){
                int di=fsd_st_dir_find(f);
                if(di>=0){
                    if(di<st_dn-1){ st_dirs[di][0]=0; for(int k=di;k<st_dn-1;k++) spud_ncpy(st_dirs[k], st_dirs[k+1], 48); }
                    st_dn--;
                    resp[0]='1'; resp[1]=0; return 1;
                }
            }
            resp[0]='0'; resp[1]=0;
            return 1;
        }
        long r=spud_sys(SYS_FS_REMOVE, (uint64_t)(uintptr_t)f, 0, 0, 0, 0);
        resp[0]=(r==0)?'1':'0'; resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_FS_MKDIR){
        const char *path=req;
        char f[64]; spud_ncpy(f, path ? path : "", sizeof(f));
        audit_push('m', from, f);
        if(fsd_st_owns(f)){
            resp[0]=(fsd_st_mkdir(f)==0)?'1':'0'; resp[1]=0;
            return 1;
        }
        long r=spud_sys(SYS_FS_CREATE, (uint64_t)(uintptr_t)f, 1, 0, 0, 0);
        resp[0]=(r==0)?'1':'0'; resp[1]=0;
        return 1;
    }
    if(type==IPC_TYPE_FS_AUDIT){
        /* trilho do servico: ultimas ops registradas (quem / o quê) */
        size_t used=0;
        for(int k=0;k<AUDIT_MAX && used+8+AUDIT_PATH+1<resp_max;k++){
            int ridx = audit_n - 1 - k;               /* mais recente primeiro */
            if(ridx < 0) break;
            int i = ridx % AUDIT_MAX;
            resp[used++] = audit_op[i] ? audit_op[i] : ' ';
            resp[used++] = 'p';
            resp[used++] = 'i';
            resp[used++] = 'd';
            resp[used++] = (char)('0' + (audit_from[i] % 10));
            resp[used++] = ' ';
            for(int j=0; audit_path[i][j] && j<AUDIT_PATH-1 && used<resp_max-1; j++)
                resp[used++] = audit_path[i][j];
            resp[used++] = '\n';
        }
        resp[used]=0;
        return (int)used;
    }
    spud_ncpy(resp, "?", resp_max);
    return 1;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    /* ABI 1.3: sonda do malloc userspace (SYS_MMAP) — roda aqui no kormi,
       pre-login, para a regressao validar o heap sem depender do teclado. */
    {
        char *probe=(char*)spud_malloc(200);
        if(probe){
            for(int i=0;i<199;i++) probe[i]=(char)('a'+(i%26));
            probe[199]=0;
            spud_write("[fsd] heap-probe ok conteudo='");
            spud_write(probe);
            spud_write("'\n");
            spud_free(probe);
        } else {
            spud_write("[fsd] heap-probe FALHOU\n");
        }
    }
    {
        enum { NB=30 };
        char *ptrs[NB];
        int okc=1;
        size_t heap_pre=spud_heap_end-spud_heap_va;
        for(int i=0;i<NB;i++){ ptrs[i]=(char*)spud_malloc(64+i*3); if(!ptrs[i]) okc=0; }
        for(int i=NB-1;i>=0;i--) spud_free(ptrs[i]);
        char *big=(char*)spud_malloc(64*NB+NB*40);
        size_t heap_post=spud_heap_end-spud_heap_va;
        int ok= okc && big && heap_post==heap_pre;
        spud_write(ok ? "[fsd] heap coalesce ok\n" : "[fsd] heap coalesce FALHOU\n");
        spud_free(big);
    }
    /* ABI 1.5: fsd vira dono do store userspace (/paradosis e /nomos em
       copia no heap) — semente vem do kernel, dali em diante e local. */
    fsd_st_seed();
    spud_svc_serve("fsd", fsd_handler);
    /* so sai daqui se o registro falhou */
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 0;
}