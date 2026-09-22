#include "thais.h"
#include "hello_elf.h"
#include "spoudazo_elf.h"
#include "praxia_elf.h"
#include "ls_elf.h"
#include "cat_elf.h"
#include "echo_elf.h"
#include "ps_elf.h"
#include "mem_elf.h"
#include "fsd_elf.h"
#include "devd_elf.h"
#include "odigos_pliktrologiou_elf.h"
#include "svctest_elf.h"
#include "wtest_elf.h"
#include "phrourio_elf.h"
#include "authd_elf.h"
#include "hello_cfg.h"
#include "spoudazo_cfg.h"
#include "praxia_cfg.h"
#include "ls_cfg.h"
#include "cat_cfg.h"
#include "echo_cfg.h"
#include "ps_cfg.h"
#include "mem_cfg.h"
#include "fsd_cfg.h"
#include "devd_cfg.h"
#include "odigos_pliktrologiou_cfg.h"
#include "svctest_cfg.h"
#include "wtest_cfg.h"
#include "phrourio_cfg.h"
#include "authd_cfg.h"
#include "synd_elf.h"
#include "synd_cfg.h"
#include <stdbool.h>

/* RAMFS — filesystem em memoria, confiavel e com ownership/permissoes.
   Toda operacao retorna FS_OK ou codigo de erro negativo. */

/* ---- Manifesto de modulo (.cfg): chaves key=value simples. ---- */

/* procura o valor (inteiro) de uma chave no manifesto; formato:
   campo = sim|nao|1|0|true|false  (e variantes). Retorna 1 se "sim". */
static bool mod_flag(const char *cfg, size_t cfg_size, const char *chave){
    size_t cl=strlen(chave);
    const char *p=cfg, *end=cfg+cfg_size;
    while(p<end){
        const char *line_s=p, *line_e=p;
        while(line_e<end && *line_e!='\n') line_e++;
        size_t ls=0;
        while(line_s<line_e && (*line_s==' '||*line_s=='\t')) line_s++;
        size_t le=line_e-line_s;
        if(le>0 && line_s[le-1]=='\r') le--;
        if(line_s+cl<=line_e && strncmp(line_s,chave,cl)==0 &&
           (line_s+cl==line_e || line_s[cl]==' '||line_s[cl]=='='||line_s[cl]==':'||line_s[cl]=='\t')){
            const char *v=line_s+cl;
            while(v<line_e && (*v==' '||*v=='='||*v==':'||*v=='\t')) v++;
            if(v<line_e && (
                strncmp(v,"sim",3)==0 || strncmp(v,"yes",3)==0 ||
                strncmp(v,"true",4)==0 || strncmp(v,"1",1)==0)){
                return true;
            }
            return false;
        }
        p=line_e+1;
    }
    return false;
}

bool mod_is_kormi(const char *cfg, size_t cfg_size){
    return mod_flag(cfg,cfg_size,"kormi");
}

/* ---- Tabela global de modulos userspace (ABI 1.5) ----
   Antes local a fs_init_defaults; agora em file-scope p/ permitir execucao
   DIRETA dos bytes embutidos (exec_embedded_args) sem passar pelo ramfs —
   desacopla o boot/exec da dependencia do FS (enabler da migracao fs→servico). */
typedef struct {
    const char *nome;
    const unsigned char *elf; size_t elf_size;
    const unsigned char *cfg; size_t cfg_size;
} mod_rec_t;

static const mod_rec_t s_mods[]={
    { "hello",    hello_elf,    HELLO_ELF_SIZE,    hello_cfg,    HELLO_CFG_SIZE },
    { "spoudazo", spoudazo_elf, SPOUDAZO_ELF_SIZE, spoudazo_cfg, SPOUDAZO_CFG_SIZE },
    { "praxia",   praxia_elf,   PRAXIA_ELF_SIZE,   praxia_cfg,   PRAXIA_CFG_SIZE },
    { "ls",       ls_elf,       LS_ELF_SIZE,       ls_cfg,       LS_CFG_SIZE },
    { "cat",      cat_elf,      CAT_ELF_SIZE,      cat_cfg,      CAT_CFG_SIZE },
    { "echo",     echo_elf,     ECHO_ELF_SIZE,     echo_cfg,     ECHO_CFG_SIZE },
    { "ps",       ps_elf,       PS_ELF_SIZE,       ps_cfg,       PS_CFG_SIZE },
    { "mem",      mem_elf,      MEM_ELF_SIZE,      mem_cfg,      MEM_CFG_SIZE },
    { "fsd",      fsd_elf,      FSD_ELF_SIZE,      fsd_cfg,      FSD_CFG_SIZE },
    { "devd",     devd_elf,     DEVD_ELF_SIZE,     devd_cfg,     DEVD_CFG_SIZE },
    { "odigos_pliktrologiou", odigos_pliktrologiou_elf, ODIGOS_PLIKTROLOGIOU_ELF_SIZE,
                                          odigos_pliktrologiou_cfg, ODIGOS_PLIKTROLOGIOU_CFG_SIZE },
    { "svctest",  svctest_elf,  SVCTEST_ELF_SIZE,  svctest_cfg,  SVCTEST_CFG_SIZE },
    { "wtest",    wtest_elf,    WTEST_ELF_SIZE,    wtest_cfg,    WTEST_CFG_SIZE },
    { "phrourio", phrourio_elf, PHROURIO_ELF_SIZE, phrourio_cfg, PHROURIO_CFG_SIZE },
    { "authd",    authd_elf,    AUTHD_ELF_SIZE,    authd_cfg,    AUTHD_CFG_SIZE },
    { "synd",     synd_elf,     SYND_ELF_SIZE,     synd_cfg,     SYND_CFG_SIZE },
};
#define S_MODS_N (sizeof(s_mods)/sizeof(s_mods[0]))

size_t fs_mod_count(void){ return S_MODS_N; }
const char *fs_mod_name(size_t i){ return (i<S_MODS_N) ? s_mods[i].nome : 0; }
const unsigned char *fs_mod_elf(size_t i, size_t *out_size){
    if(i>=S_MODS_N) return 0;
    if(out_size) *out_size=s_mods[i].elf_size;
    return s_mods[i].elf;
}
const unsigned char *fs_mod_cfg(size_t i, size_t *out_size){
    if(i>=S_MODS_N) return 0;
    if(out_size) *out_size=s_mods[i].cfg_size;
    return s_mods[i].cfg;
}
bool fs_mod_is_kormi(size_t i){
    return (i<S_MODS_N) ? mod_flag(s_mods[i].cfg, s_mods[i].cfg_size, "kormi") : false;
}

static fs_node_t fs_nodes[FS_MAX_FILES];
static int fs_count=0;

void fs_init(void){
    for(int i=0;i<FS_MAX_FILES;i++){
        fs_nodes[i].used=false; fs_nodes[i].data=0; fs_nodes[i].type=FS_TYPE_FILE;
        fs_nodes[i].path[0]=0; fs_nodes[i].size=0; fs_nodes[i].capacity=0;
        fs_nodes[i].caps_req=CAP_NONE; fs_nodes[i].owner=0xFF;
        fs_nodes[i].world_read=true; fs_nodes[i].world_write=true;
        fs_nodes[i].type=FS_TYPE_FILE;
    }
    fs_count=0;
    kprint("[arkhe] VFS (ramfs) pronto\n");
}

fs_node_t* fs_get(const char *path){
    for(int i=0;i<FS_MAX_FILES;i++) if(fs_nodes[i].used && strcmp(fs_nodes[i].path,path)==0) return &fs_nodes[i];
    return 0;
}

static fs_node_t* fs_find_node(const char *path){ return fs_get(path); }

bool fs_exists(const char *path){
    if(strcmp(path,"/")==0) return true;
    return fs_find_node(path)!=0;
}
bool fs_is_dir(const char *path){
    if(strcmp(path,"/")==0) return true;
    fs_node_t *n=fs_find_node(path); return n && n->type==FS_TYPE_DIR;
}

/* verifica se existe filho com o prefixo path+"/" (dir nao vazio) */
static bool fs_has_children(const char *path){
    size_t dl=strlen(path);
    for(int i=0;i<FS_MAX_FILES;i++){
        if(!fs_nodes[i].used) continue;
        const char *p=fs_nodes[i].path;
        if(strncmp(p,path,dl)!=0) continue;
        if(p[dl]=='/') return true;
    }
    return false;
}

int fs_create(const char *path, fs_type_t type){
    if(strcmp(path,"/")==0) return FS_ERROR;
    if(fs_find_node(path)) return FS_ERROR;
    fs_node_t *n=0;
    for(int i=0;i<FS_MAX_FILES;i++) if(!fs_nodes[i].used){ n=&fs_nodes[i]; break; }
    if(!n){ kprint("[arkhe] VFS cheia\n"); return FS_ERROR; }

    /* valida pai existe e e diretorio */
    const char *slash=0;
    for(const char *p=path;*p;p++) if(*p=='/') slash=p;
    if(slash && slash!=path){
        char parent[FS_NAME_MAX];
        size_t len=slash-path; if(len>=FS_NAME_MAX) return FS_ERROR;
        strncpy(parent,path,len); parent[len]=0;
        if(!fs_is_dir(parent)) return FS_ERROR;         /* pai nao existe */
    } else if(slash==path){
        if(strlen(path)==1) return FS_ERROR;            /* "/" ja existe */
        if(!fs_is_dir("/")) return FS_ERROR;
    }

    strncpy(n->path,path,FS_NAME_MAX);
    n->path[FS_NAME_MAX-1]=0;
    n->type=type;
    n->used=true;
    n->size=0; n->capacity=0; n->data=0; n->caps_req=CAP_NONE;
    /* ownership padrao: current process (se houver) ou sistema (0xFF) */
    n->owner = auth_user_index_current();
    n->world_read = (n->owner==0xFF);
    n->world_write = false;
    fs_count++;
    return FS_OK;
}

int fs_write(const char *path, const void *data, size_t size){
    if(size > FS_MAX_DATA) return FS_ERROR;
    fs_node_t *n=fs_find_node(path);
    if(!n || n->type==FS_TYPE_DIR) return FS_ERROR;
    if(n->capacity < size){
        size_t new_cap = n->capacity ? n->capacity*2 : 256;
        while(new_cap < size) new_cap*=2;
        uint8_t *nd=kmalloc(new_cap);
        if(!nd) return FS_ERROR;
        if(n->data) kfree(n->data);       /* libera o buffer antigo, sem copia redundante */
        n->data=nd; n->capacity=new_cap;
    }
    if(size && data) memcpy(n->data,data,size);
    n->size=size;
    return FS_OK;
}

int fs_read(const char *path, void *buf, size_t max, size_t *out_size){
    fs_node_t *n=fs_find_node(path);
    if(!n || n->type==FS_TYPE_DIR) return FS_ERROR;
    size_t to=n->size; if(to>max) to=max;
    if(to && buf) memcpy(buf,n->data,to);
    if(out_size) *out_size=to;
    return FS_OK;
}

int fs_remove(const char *path){
    if(strcmp(path,"/")==0) return FS_ERROR;
    fs_node_t *n=fs_find_node(path);
    if(!n) return FS_ERROR;
    if(n->type==FS_TYPE_DIR && fs_has_children(path)) return FS_ERROR;  /* dir nao vazio */
    if(n->data) kfree(n->data);
    n->used=false; n->data=0; n->size=0; n->capacity=0;
    if(fs_count>0) fs_count--;
    return FS_OK;
}

int fs_copy(const char *src, const char *dst){
    fs_node_t *s=fs_find_node(src);
    if(!s || s->type==FS_TYPE_DIR) return FS_ERROR;
    if(fs_find_node(dst)) return FS_ERROR;
    if(fs_create(dst,FS_TYPE_FILE)!=FS_OK) return FS_ERROR;
    return fs_write(dst,s->data,s->size);
}

int fs_move(const char *src, const char *dst){
    int r=fs_copy(src,dst);
    if(r!=FS_OK) return r;
    return fs_remove(src);
}

int fs_list(const char *dir, char *out, size_t out_max){
    size_t o=0; bool any=false;
    size_t dl=strlen(dir);
    for(int i=0;i<FS_MAX_FILES;i++){
        if(!fs_nodes[i].used) continue;
        const char *p=fs_nodes[i].path;
        if(strcmp(p,dir)==0) continue;
        bool show=false;
        if(strcmp(dir,"/")==0){
            if(p[0]!='/') continue;
            const char *rest=p+1;
            if(!*rest || strchr(rest,'/')) continue;
            show=true;
        } else {
            if(strncmp(p,dir,dl)!=0) continue;
            if(p[dl]!='/') continue;
            if(strchr(p+dl+1,'/')) continue;
            show=true;
        }
        if(show){
            any=true;
            const char *name = (strcmp(dir,"/")==0) ? p+1 : p+dl+1;
            int need=snprintf(out+o, o<out_max?out_max-o:0, "  %s   %s\n",
                fs_nodes[i].type==FS_TYPE_DIR?"<DIR>":"<ARQ>", name);
            if(need<0 || o+((size_t)need)>=out_max) break;
            o+=(size_t)need;
        }
    }
    if(!any){
        int need=snprintf(out+o, o<out_max?out_max-o:0, "  (vazio)\n");
        if(need>0 && o+((size_t)need)<out_max) o+=(size_t)need;
    }
    if(out && out_max>0) out[o]=0;
    return FS_OK;
}

void fs_init_defaults(void){
    /* Estrutura de pastas do Siaht OS (userspace enxuto):
       /bin        — aplicacoes do usuario (executaveis + .cfg de manifesto)
       /kormi      — apps iniciados com o sistema ANTES do login (drivers,
                     servicos essenciais); sao ELF normais, spawnados por init
       /praxis     — sessao: spoudazo (login) e praxia (shell) (aliases de /bin)
       /idios      — lar do individuo (home)
       /oikos      — sistema interno (read-only para usuarios)
       /synallagma — contratos
       /aisthesis, /kinesis — pseudo-FS (leitura dinamica)
       /nomos      — manifesto/leis (texto)
       /paradosis  — tradicao/exemplos (texto) */
    const char *dirs[]={
        "/","/bin","/kormi","/praxis","/idios","/idios/thais",
        "/oikos","/synallagma","/aisthesis","/kinesis","/nomos","/paradosis",0
    };
    for(int i=0;dirs[i];i++) fs_create(dirs[i],FS_TYPE_DIR);
    const char *welcome="Siaht OS - Made with love by Thais (op3n/op3ny)\nSistema de arquivos voluntario (ramfs), sem root.\nTente: horasis, metabasis, ktisis, graphe\n";
    fs_create("/idios/thais/boas_vindas.txt",FS_TYPE_FILE);
    fs_write("/idios/thais/boas_vindas.txt",welcome,strlen(welcome));
    const char *manifest="Praxeologia: toda acao e praxis. Sem coercente central, ordem emerge de contratos.\n";
    fs_create("/nomos/manifesto.txt",FS_TYPE_FILE);
    fs_write("/nomos/manifesto.txt",manifest,strlen(manifest));
    fs_create("/paradosis/exemplo.txt",FS_TYPE_FILE);
    fs_write("/paradosis/exemplo.txt","exemplo de arquivo em /paradosis\n",32);
    /* contratos de exemplo que restringem pastas (decisao voluntaria no login) */
    const char *c_idios =
        "[titulo]\nnome = \"Acesso ao lar do individuo\"\n\n"
        "caminho = \"/idios\"\n"
        "leitura = true\n"
        "escrita = true\n"
        "execucao = false\n"
        "voluntario = true\n"
        "revogavel = true\n";
    fs_create("/synallagma/idios.pacto",FS_TYPE_FILE);
    fs_write("/synallagma/idios.pacto",c_idios,strlen(c_idios));

    /* ---- Apps userspace (ABI 1.2). Cada <name> tem:
         /bin/<name>        — ELF64 (existe sempre)
         /bin/<name>.cfg    — manifesto (nome/alias/desc/kormi), lido pela shell
         /kormi/<name>      — se manifesto diz kormi=1, copia p/ iniciar no boot
       A TABELA s_mods[] (file-scope acima) alimenta o ramfs E a execucao
       embutida (exec_embedded_args) — a arvore /bin,/kormi segue existindo
       p/ visibilidade (ls/cat), mas o boot/exec nao depende mais dela. */
    for(size_t i=0;i<S_MODS_N;i++){
        const char *nome=s_mods[i].nome;
        char bin[FS_NAME_MAX];
        snprintf(bin,FS_NAME_MAX,"/bin/%s",nome);
        fs_create(bin,FS_TYPE_FILE);
        fs_write(bin,s_mods[i].elf,s_mods[i].elf_size);
        char cfgb[FS_NAME_MAX];
        snprintf(cfgb,FS_NAME_MAX,"/bin/%s.cfg",nome);
        fs_create(cfgb,FS_TYPE_FILE);
        fs_write(cfgb,s_mods[i].cfg,s_mods[i].cfg_size);
        kprint(bin);
    }
    /* /praxis = aliases de sessao (init/spoudazo referenciam direto) */
    fs_copy("/bin/spoudazo","/praxis/spoudazo");
    fs_copy("/bin/praxia","/praxis/praxia");
    /* kormi: apps de boot (a execucao do boot usa os bytes embutidos direto,
       fs_mod_is_kormi == mesma leitura do manifesto; /kormi apenas espelha). */
    for(size_t i=0;i<S_MODS_N;i++){
        if(!fs_mod_is_kormi(i)) continue;
        char dst[FS_NAME_MAX];
        snprintf(dst,FS_NAME_MAX,"/kormi/%s",s_mods[i].nome);
        char src[FS_NAME_MAX];
        snprintf(src,FS_NAME_MAX,"/bin/%s",s_mods[i].nome);
        fs_copy(src,dst);
    }
    kprint("[arkhe] arvore Siaht OS montada (modulos em /bin e /kormi)\n");
}
