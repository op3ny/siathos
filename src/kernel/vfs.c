#include "thais.h"
#include <stdbool.h>

/* VFS — interface unica entre userspace/servicos e o filesystem (ramfs hoje,
   ThaFS futuramente). Faz normalizacao de path, valida permissao/capability
   e delega ao fs_* por baixo. Retorna FS_OK/FS_ERROR consistentemente.

   MARCO 7 — pseudo-FS (/aisthesis, /kinesis): arquivos virtuais gerados
   dinamicamente no vfs_read/vfs_list. Conteudo nunca e persistido —
   sempre recalculado a partir do estado do kernel. */

void vfs_init(void){ fs_init(); }

/* ---- MARCO 7: pseudo-FS ---- */
/* /aisthesis — dados do sistema (memoria, processos)
   /kinesis  — dados do scheduler (processos detalhados, uptime) */

static bool vfs_is_pseudo(const char *norm){
    return (norm[0]=='/' && (
        (strncmp(norm,"/aisthesis",10)==0 && (norm[10]=='/' || norm[10]==0)) ||
        (strncmp(norm,"/kinesis",8)==0  && (norm[8]=='/' || norm[8]==0))));
}

/* Gera o conteudo do pseudo-FS para o path normalizado.
   Retorna bytes preenchidos em out, ou 0 se path invalido. */
static size_t vfs_pseudo_read(const char *norm, char *out, size_t max){
    out[0]=0; size_t used=0;
    #define APPEND(s) do{ size_t l=strlen(s); if(used+l<max){ memcpy(out+used,(s),l); used+=l; } }while(0)
    #define APPEND_DEC(v) do{ char db[24]; snprintf(db,sizeof(db),"%llu",(unsigned long long)(v)); APPEND(db); }while(0)

    if(strcmp(norm,"/aisthesis/memoria")==0){
        uint64_t total=pmm_total_mem(), free=pmm_free_mem();
        APPEND("Memoria total : "); APPEND_DEC(total); APPEND(" bytes\n");
        APPEND("Memoria livre : "); APPEND_DEC(free);  APPEND(" bytes\n");
        APPEND("Memoria usada : "); APPEND_DEC(total-free); APPEND(" bytes\n");
    } else if(strcmp(norm,"/aisthesis/processos")==0){
        system_info_t si;
        if(sysinfo_get(&si)==FS_OK){
            APPEND("Processos ativos : "); APPEND_DEC(si.process_count); APPEND("\n");
            uint32_t term=0;
            proc_info_t buf[64];
            int n=sysinfo_proc_list(buf,64);
            for(int i=0;i<n;i++) if(buf[i].state==PROC_TERMINATED) term++;
            APPEND("Processos terminados : "); APPEND_DEC(term); APPEND("\n");
        }
    } else if(strcmp(norm,"/kinesis/processos")==0){
        proc_info_t buf[64];
        int n=sysinfo_proc_list(buf,64);
        for(int i=0;i<n;i++){
            const char *st="???";
            switch(buf[i].state){
                case PROC_CREATED:   st="criado";   break;
                case PROC_READY:     st="pronto";   break;
                case PROC_RUNNING:   st="rodando";  break;
                case PROC_BLOCKED:   st="bloqueado"; break;
                case PROC_TERMINATED:st="terminado"; break;
            }
            char line[128];
            snprintf(line,sizeof(line),"[%u] %s %s %s\n",
                buf[i].pid, buf[i].name, st,
                buf[i].is_user?"ring3":"ring0");
            APPEND(line);
        }
    } else if(strcmp(norm,"/kinesis/uptime")==0){
        APPEND("Uptime : "); APPEND_DEC(ticks*10); APPEND(" ms\n");
        APPEND("Ticks  : "); APPEND_DEC(ticks); APPEND("\n");
    } else {
        out[0]=0; return 0;
    }
    #undef APPEND
    #undef APPEND_DEC
    return used;
}

/* Lista entradas de um diretorio pseudo-FS. */
static bool vfs_pseudo_list(const char *norm, char *out, size_t max){
    if(strcmp(norm,"/aisthesis")==0){
        snprintf(out,max,"memoria\nprocessos\n"); return true;
    } else if(strcmp(norm,"/kinesis")==0){
        snprintf(out,max,"processos\nuptime\n"); return true;
    }
    return false;
}

int vfs_normalize(const char *cwd, const char *in, char *out, size_t out_max){
    char tmp[FS_NAME_MAX*2];
    if(!in || !*in){ strncpy(out, cwd?cwd:"/", out_max); out[out_max-1]=0; return FS_OK; }
    if(in[0]=='/') strncpy(tmp, in, sizeof(tmp));
    else snprintf(tmp, sizeof(tmp), "%s/%s", (cwd && strcmp(cwd,"/")!=0)?cwd:"", in);
    tmp[sizeof(tmp)-1]=0;
    out[0]='/'; out[1]=0;
    char *p=tmp;
    while(*p){
        while(*p=='/') p++;
        if(!*p) break;
        char *start=p;
        while(*p && *p!='/') p++;
        size_t len=p-start;
        char comp[32];
        if(len>=sizeof(comp)) len=sizeof(comp)-1;
        memcpy(comp, start, len); comp[len]=0;
        if(strcmp(comp,".")==0) continue;
        if(strcmp(comp,"..")==0){
            char *last=strrchr(out,'/');
            if(last && last!=out) *last=0;
            else if(last==out) out[1]=0;
            continue;
        }
        size_t out_len=strlen(out);
        if(out_len>1){
            if(out_len+1+len >= out_max) return FS_ERROR;   /* path muito longo */
            out[out_len]='/';
            memcpy(out+out_len+1, comp, len);
            out[out_len+1+len]=0;
        } else {
            if(1+len >= out_max) return FS_ERROR;
            memcpy(out+1, comp, len);
            out[1+len]=0;
        }
    }
    if(out[0]==0) strcpy(out,"/");
    return FS_OK;
}

/* vfs_check_cap — politica de acesso por pasta, definida em contratos.
   Por padrao TUDO e acessivel, exceto /oikos (read-only) e pastas com
   contrato (regem-se por ele apos consentimento no login). /synallagma
   (contratos) e escrita apenas para o soberano. */
bool vfs_check_cap(const char *path, uint64_t need){
    bool write_op = (need & (CAP_FS_WRITE|CAP_IDIOS_WRITE|CAP_NOMOS_WRITE)) != 0;
    auth_user_t *u=login_result_user();
    bool admin = u && u->is_admin;
    bool proc_cap = current_proc && (current_proc->rights & need);

    /* 1) contrato que protege a pasta (ou um ancestral) */
    contract_t *c=synallagma_contract_for(path);
    if(c){
        if(!synallagma_consented(c->caminho)) return false;      /* sem consentimento */
        if(write_op && !c->escrita) return false;
        if(!write_op && !c->leitura && !c->execucao) return false;
        if(c->caps_req && !admin && !(u && (u->caps & c->caps_req)) && !proc_cap) return false;
        return true;
    }

    /* 2) /oikos: sistema interno, read-only POR PADRAO para todos (inclusive
       soberano). A unica pasta que precisa de escrita p/ alguem e /synallagma. */
    if(path[0]=='/' && strncmp(path,"/oikos",6)==0 && (path[6]=='/' || path[6]==0)){
        if(write_op) return false;
        return true;   /* leitura aberta */
    }

    /* 3) /synallagma: contratos; escrita so p/ soberano (pelo menos um user) */
    if(path[0]=='/' && strncmp(path,"/synallagma",11)==0 && (path[11]=='/' || path[11]==0)){
        if(write_op) return admin || (u && (u->caps & need)) || proc_cap;
        return true;
    }

    /* 4) padrao: aberto (filosofia: ordem espontanea, acesso voluntario) */
    return true;
}

int vfs_open(const char *cwd, const char *path, char *out_full){
    char norm[FS_NAME_MAX];
    if(!path || !*path){ if(out_full) strncpy(out_full,cwd?cwd:"/",FS_NAME_MAX); return FS_OK; }
    if(vfs_normalize(cwd, path, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(strlen(norm)>=FS_NAME_MAX) return FS_ERROR;
    if(!fs_exists(norm)) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_READ)) return FS_ERROR;
    if(out_full) strncpy(out_full,norm,FS_NAME_MAX);
    return FS_OK;
}

int vfs_create(const char *cwd, const char *path, fs_type_t type){
    char norm[FS_NAME_MAX];
    if(vfs_normalize(cwd, path, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_WRITE)) return FS_ERROR;
    return fs_create(norm, type);
}

int vfs_mkdir(const char *cwd, const char *path){
    return vfs_create(cwd, path, FS_TYPE_DIR);
}

int vfs_read(const char *cwd, const char *path, void *buf, size_t max, size_t *out_size){
    char norm[FS_NAME_MAX];
    if(vfs_normalize(cwd, path, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_READ)) return FS_ERROR;
    /* MARCO 7: pseudo-FS — conteudo gerado dinamicamente */
    if(vfs_is_pseudo(norm)){
        size_t n=vfs_pseudo_read(norm, (char*)buf, max);
        if(out_size) *out_size=n;
        return n>0?FS_OK:FS_ERROR;
    }
    return fs_read(norm, buf, max, out_size);
}

int vfs_write(const char *cwd, const char *path, const void *data, size_t size){
    char norm[FS_NAME_MAX];
    if(vfs_normalize(cwd, path, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_WRITE)) return FS_ERROR;
    /* MARCO 7: pseudo-FS e somente leitura */
    if(vfs_is_pseudo(norm)) return FS_ERROR;
    if(!fs_exists(norm)){
        if(fs_create(norm,FS_TYPE_FILE)!=FS_OK) return FS_ERROR;
    }
    return fs_write(norm, data, size);
}

int vfs_remove(const char *cwd, const char *path){
    char norm[FS_NAME_MAX];
    if(vfs_normalize(cwd, path, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_WRITE)) return FS_ERROR;
    return fs_remove(norm);
}

int vfs_copy(const char *cwd, const char *src, const char *dst){
    char nsrc[FS_NAME_MAX], ndst[FS_NAME_MAX];
    if(vfs_normalize(cwd, src, nsrc, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(vfs_normalize(cwd, dst, ndst, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(nsrc, CAP_FS_READ)) return FS_ERROR;
    if(!vfs_check_cap(ndst, CAP_FS_WRITE)) return FS_ERROR;
    return fs_copy(nsrc, ndst);
}

int vfs_move(const char *cwd, const char *src, const char *dst){
    char nsrc[FS_NAME_MAX], ndst[FS_NAME_MAX];
    if(vfs_normalize(cwd, src, nsrc, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(vfs_normalize(cwd, dst, ndst, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(nsrc, CAP_FS_READ)) return FS_ERROR;
    if(!vfs_check_cap(ndst, CAP_FS_WRITE)) return FS_ERROR;
    return fs_move(nsrc, ndst);
}

int vfs_list(const char *cwd, const char *path, char *out, size_t out_max){
    char norm[FS_NAME_MAX];
    if(vfs_normalize(cwd, path?path:cwd, norm, FS_NAME_MAX)!=FS_OK) return FS_ERROR;
    if(!vfs_check_cap(norm, CAP_FS_READ)) return FS_ERROR;
    /* MARCO 7: pseudo-FS — /aisthesis e /kinesis como diretorios virtuais */
    if(vfs_is_pseudo(norm) && vfs_pseudo_list(norm, out, out_max)) return FS_OK;
    return fs_list(norm, out, out_max);
}
