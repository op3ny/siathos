#include "thais.h"
#include <stdbool.h>

/* syscall.c — tabela de syscalls + dispatcher (Phase J).
   A ABI e: syscall_dispatch(numero, a1, a2, a3, a4, a5).
   Em modo kernel-cooperativo, os processos de sistema chamam esta funcao
   diretamente; em anel 3, ela e a origem do syscall instruction (Phase K).

   AUDITORIA (seguranca): todo argumento que aponta para memoria do chamador
   (escrita de saida, buffers de FS/IPC, strings) e validado antes de copiar.
   Um processo ring-3 so pode tocar regioes que o kernel mapeou PARA ELE
   (process_t.umap_*). Processos ring-0 (kernel) confiam no espaco do kernel. */

void syscall_init(void){
    kprint("[arkhe] syscall ABI pronta\n");
}

#define SYSCALL_NAME_MAX 12

/* Verifica se o processo atual tem a capability 'need'. */
static bool syscall_cap(uint64_t need){
    if(!current_proc) return true;
    if(current_proc->rights == CAP_ALL) return true;
    return (current_proc->rights & need) != 0;
}

/* ---- Validacao de ponteiros (Auditoria de Security) ----
   Ensures: o processo atual pode acessar [addr, addr+len)? Para ring-3,
   addr deve estar dentro de uma regiao mapeada para o processo (umap_*).
   Overflow-checks a3/a4 etc. Ser ring-0 significa que o ponteiro veio do
   proprio kernel (ou de um binario que o kernel carregou) e e confiado. */
static bool user_ptr_ok(const void *addr, size_t len){
    if(!addr) return len==0;
    uintptr_t a=(uintptr_t)addr;
    if(len==0) return true;
    if(a+len < a) return false;                    /* wraparound */
    if(!current_proc) return false;                /* syscalls vem de um processo */
    if(!current_proc->is_user) return true;        /* ring-0: confia no kernel */
    for(int i=0;i<current_proc->umap_count;i++){
        uint64_t b=current_proc->umap_base[i], sz=current_proc->umap_size[i];
        if(sz==0) continue;
        if(a>=b && (uint64_t)(a-b) < sz && (uint64_t)(a-b)+len <= sz) return true;
    }
    return false;                                  /* fora do espaco do processo */
}

/* Copia 'n' bytes do espaco do chamador. Se o ponteiro nao for valido, nao
   copia e retorna false (o syscall deve retornar erro). */
static bool user_copy(void *dst, const void *src, size_t n){
    if(!user_ptr_ok(src, n)) return false;
    if(n) memcpy(dst, src, n);
    return true;
}

/* Le uma string NUL-terminated do chamador (no maximo 'dst_max' bytes).
   Valida o ponteiro byte a byte dentro do espaco do processo. */
static bool user_str(const char *src, char *dst, size_t dst_max){
    if(!src || !dst || dst_max==0) return false;
    size_t i=0;
    if(!current_proc) return false;
    for(;;){
        uint8_t c;
        if(!user_ptr_ok(&src[i], 1)) return false;
        c=*((const uint8_t*)src+i);
        if(c==0){ dst[i]=0; return true; }
        if(i+1>=dst_max){ return false; }
        dst[i]=(char)c;
        i++;
    }
}

uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5){
    (void)a4;(void)a5;
    /* validacao de capacidade de SYSCALL (todos os processos tem se tiverem CAP_SYSCALL) */
    if(current_proc && !(current_proc->rights & CAP_SYSCALL) && current_proc->rights!=CAP_ALL){
        kprint("[arkhe] syscall negada: sem CAP_SYSCALL\n");
        return 0xFFFFFFFFFFFFFFFFULL;
    }
    switch(nr){
        case SYS_EXIT:
            proc_exit((int)a1);
            return 0;
        case SYS_WRITE: {
            /* ABI real (hello.asm/ring3demo.asm): a1 = fd, a2 = ptr, a3 = tamanho */
            size_t n=(size_t)a3;
            if(n > 4096) n=4096;              /* limite por chamada */
            char tmp[4097];
            if(!user_copy(tmp, (const void*)a2, n)) return 0xFFFFFFFFFFFFFFFFULL;
            tmp[n]=0;
            fb_console_write(tmp);
            serial_write(tmp);
            return n;
        }
        case SYS_READ: {
            /* a1 = buf, a2 = max — le uma tecla do teclado. Com o driver de
               teclado ring 3 ('odigos_pliktrologiou', servico 'kbd')
               registrado, a leitura vem do servico via IPC (arquitetura-
               alvo); sem o servico, leitura direta das portas PS/2
               (fallback ring 0). */
            char c=0;
            uint32_t kbd=0;
            int rv=-9;
            int sq=svc_query("kbd", &kbd);
            if(sq==0 && kbd>0){
                char rq='g';
                int sr=ipc_send(kbd, IPC_TYPE_KBD_GET, &rq, 1);
                rv=sr;
                if(sr==0){
                    uint32_t from=0, type=0; size_t got=0;
                    char resp[8];
                    rv=ipc_try_receive(&from, &type, resp, sizeof(resp), &got);
                    if(rv==0 && got>=1){
                        c=resp[0];
                    }
                }
            } else {
                bool ok=false;
                c=keyboard_getc_nonblock(&ok);
                if(!ok) c=0;
                rv=ok?1:0;
            }
            if(c==0) return 0;
            if(a1 && a2>0){
                if(!user_ptr_ok((const void*)a1, 1)) return 0xFFFFFFFFFFFFFFFFULL;
                *((char*)a1)=c;
            }
            return 1;
        }
        case SYS_EXEC: {
            /* ABI MARCO 3: a1=path, a2=argc, a3=argv (array user de char*).
               Valida o array de argumentos e delega ao elf loader. */
            if(!a1) return 0xFFFFFFFFFFFFFFFFULL;
            if(!syscall_cap(CAP_EXEC)){ kprint("[arkhe] SYS_EXEC: sem CAP_EXEC\n"); return 0xFFFFFFFFFFFFFFFFULL; }
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            /* copia argumentos do espaco do chamador para buffers validados */
            int argc = a2 ? (int)a2 : 0;
            if(argc < 0) argc=0;
            if(argc > 16) argc=16;
            const char *kargv[16];
            char argbuf[16][128];
            memset(argbuf, 0, sizeof(argbuf));
            if(argc > 0 && a3){
                const uint64_t *user_argv = (const uint64_t*)a3;
                if(!user_ptr_ok((const void*)a3, (size_t)argc * sizeof(uint64_t)))
                    return 0xFFFFFFFFFFFFFFFFULL;
                for(int i=0;i<argc;i++){
                    const char *s = (const char*)user_argv[i];
                    if(!s){ argbuf[i][0]=0; kargv[i]=argbuf[i]; continue; }
                    if(!user_str(s, argbuf[i], 128)) return 0xFFFFFFFFFFFFFFFFULL;
                    kargv[i]=argbuf[i];
                }
            }
            uint32_t pid=0;
            int r=exec_user_path(path, argc, argc?kargv:0, &pid);
            return r==FS_OK ? (uint64_t)(uint32_t)pid : 0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_IPC_SEND: {
            if(!syscall_cap(CAP_IPC)) return 0xFFFFFFFFFFFFFFFFULL;
            uint32_t to=(uint32_t)a1, type=(uint32_t)a2;
            size_t sz=(size_t)a4;
            if(a3 && sz>0 && !user_ptr_ok((const void*)a3, sz)) return 0xFFFFFFFFFFFFFFFFULL;
            return (uint64_t)(uint32_t)ipc_send(to,type,(void*)a3,(size_t)a4);
        }
        case SYS_IPC_RECV: {
            if(!syscall_cap(CAP_IPC)) return 0xFFFFFFFFFFFFFFFFULL;
            uint32_t from=0,type=0; size_t got=0;
            if(a1 && !user_ptr_ok((const void*)a1, (size_t)a2)) return 0xFFFFFFFFFFFFFFFFULL;
            int r=ipc_try_receive(&from,&type,(void*)a1,(size_t)a2,&got);
            if(a3){
                if(!user_ptr_ok((const void*)a3, 4)) return 0xFFFFFFFFFFFFFFFFULL;
                *(uint32_t*)a3=from;
            }
            if(a4){
                if(!user_ptr_ok((const void*)a4, 4)) return 0xFFFFFFFFFFFFFFFFULL;
                *(uint32_t*)a4=type;
            }
            return r==0?got:0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_IPC_REPLY: {
            if(!syscall_cap(CAP_IPC)) return 0xFFFFFFFFFFFFFFFFULL;
            size_t sz=(size_t)a4;
            if(a3 && sz>0 && !user_ptr_ok((const void*)a3, sz)) return 0xFFFFFFFFFFFFFFFFULL;
            return (uint64_t)(uint32_t)ipc_reply((uint32_t)a1,(uint32_t)a2,(void*)a3,(size_t)a4);
        }
        case SYS_YIELD:
            /* Fix bugfix 2/3: processos ring-3 nao mais parkam a cadeia C no
               SYS_YIELD (caminho de resume quebrado). SYS_YIELD anel 3 e
               tick-only: retorna sem chavear; a alternancia fica com a
               preempcao por IRQ (stub, style=1 — caminho seguro). Processos
               do kernel (is_user=0) seguem com sched_yield (park C aceito). */
            if(current_proc && current_proc->is_user)
                return 0;
            sched_yield();
            return 0;
        case SYS_FS_WRITE: {
            if(!syscall_cap(CAP_FS_WRITE)){ kprint("[arkhe] SYS_FS_WRITE: sem CAP_FS_WRITE\n"); return 0xFFFFFFFFFFFFFFFFULL; }
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            size_t sz=(size_t)a3;
            if(!user_ptr_ok((const void*)a2, sz)) return 0xFFFFFFFFFFFFFFFFULL;
            int r=vfs_write("/", path, (void*)a2, sz);
            return r==FS_OK?0:0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_FS_READ: {
            if(!syscall_cap(CAP_FS_READ)){ kprint("[arkhe] SYS_FS_READ: sem CAP_FS_READ\n"); return 0xFFFFFFFFFFFFFFFFULL; }
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_ptr_ok((const void*)a2, (size_t)a3)) return 0xFFFFFFFFFFFFFFFFULL;
            size_t got=0;
            int r=vfs_read("/", path, (void*)a2, (size_t)a3, &got);
            return r==FS_OK?got:0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_FS_LIST: {
            if(!syscall_cap(CAP_FS_READ)){ kprint("[arkhe] SYS_FS_LIST: sem CAP_FS_READ\n"); return 0xFFFFFFFFFFFFFFFFULL; }
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            char tmp[4096];
            int r=vfs_list("/", path, tmp, sizeof(tmp));
            if(r!=FS_OK) return 0xFFFFFFFFFFFFFFFFULL;
            /* ABI: a3 = tamanho do buffer do chamador (0 = assume 4096). */
            size_t bsz = a3 ? (size_t)a3 : sizeof(tmp);
            if(bsz > sizeof(tmp)) bsz = sizeof(tmp);
            if(a2 && bsz){
                if(!user_ptr_ok((const void*)a2, bsz)) return 0xFFFFFFFFFFFFFFFFULL;
                memcpy((char*)a2, tmp, bsz);
            }
            return 0;
        }
        case SYS_FS_CREATE: {
            if(!syscall_cap(CAP_FS_WRITE)) return 0xFFFFFFFFFFFFFFFFULL;
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            fs_type_t t=(a2==1)?FS_TYPE_DIR:FS_TYPE_FILE;
            int r=vfs_create("/", path, t);
            return r==FS_OK?0:0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_FS_REMOVE: {
            if(!syscall_cap(CAP_FS_WRITE)) return 0xFFFFFFFFFFFFFFFFULL;
            char path[FS_NAME_MAX];
            if(!user_str((const char*)a1, path, sizeof(path))) return 0xFFFFFFFFFFFFFFFFULL;
            int r=vfs_remove("/", path);
            return r==FS_OK?0:0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_GETPID:
            return current_proc ? current_proc->pid : 0;
        case SYS_GETPPID:
            return current_proc ? current_proc->parent : 0;
        case SYS_UPTIME:
            /* retorna uptime em ms (ticks * 10, PIT 100Hz) */
            return ticks * 10;
        case SYS_SYSINFO: {
            if(!a1) return 0xFFFFFFFFFFFFFFFFULL;
            system_info_t info;
            if(sysinfo_get(&info)!=FS_OK) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_ptr_ok((const void*)a1, sizeof(info))) return 0xFFFFFFFFFFFFFFFFULL;
            memcpy((void*)a1, &info, sizeof(info));
            return FS_OK;
        }
        case SYS_PROC_LIST: {
            /* a1 = proc_info_t[], a2 = max entradas (0 = 64) */
            int max = a2 ? (int)a2 : (MAX_PROCS<64?MAX_PROCS:64);
            if(max<=0 || max>64) max=64;
            size_t bytes;
            int n=0;
            {
                proc_info_t buf[64];
                n = sysinfo_proc_list(buf, max);
                bytes = (size_t)n * sizeof(proc_info_t);
                if(!user_ptr_ok((const void*)a1, bytes)) return 0xFFFFFFFFFFFFFFFFULL;
                memcpy((void*)a1, buf, bytes);
            }
            return n;   /* numero REAL de processos preenchidos */
        }
        /* ---- Sessao ring 3 (MARCO 2: spoudazo, login/consentimento/shell) ---- */
        case SYS_AUTH_COUNT: {
            return (uint64_t)auth_user_count();
        }
        case SYS_AUTH_CREATE: {
            char name[AUTH_NAME_MAX], pass[AUTH_NAME_MAX];
            if(auth_user_count()!=0) return 0;                 /* so bootstrap */
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a2, pass, sizeof(pass))) return 0xFFFFFFFFFFFFFFFFULL;
            if(strlen(name)<3 || strlen(pass)<4) return 0;
            return auth_create_user(name, pass, CAP_ALL, true) ? 1 : 0;
        }
        case SYS_AUTH_VERIFY: {
            char name[AUTH_NAME_MAX], pass[AUTH_NAME_MAX];
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a2, pass, sizeof(pass))) return 0xFFFFFFFFFFFFFFFFULL;
            return auth_verify(name, pass) ? 1 : 0;
        }
        case SYS_AUTH_LIST: {            /* (buf,max) -> lista textual */
            if(!a1 || !a2) return 0xFFFFFFFFFFFFFFFFULL;
            size_t bsz=(size_t)a2; if(bsz>4096) bsz=4096;
            char tmp[4096]; tmp[0]=0;
            int n=auth_list_text(tmp,bsz);
            if(n<0 || (size_t)n>=bsz) tmp[bsz-1]=0;
            size_t out=(size_t)n+1; if(out>bsz) out=bsz;
            if(!user_ptr_ok((const void*)a1,out)) return 0xFFFFFFFFFFFFFFFFULL;
            memcpy((void*)a1,tmp,out);
            return 0;
        }
        case SYS_AUTH_DELETE: {          /* admin: (name) -> remove */
            if(!syscall_cap(CAP_USER_ADMIN)) return 0xFFFFFFFFFFFFFFFFULL;
            char name[AUTH_NAME_MAX];
            if(!user_str((const char*)a1,name,sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            auth_user_t *u=auth_find(name);
            if(!u || u->is_admin) return 0;               /* nunca deleta admin */
            if(u==login_result_user()) return 0;          /* nem a si mesmo */
            return auth_user_delete(name) ? 1 : 0;
        }
        case SYS_AUTH_CHANGEPW: {        /* (user,old,new) */
            char name[AUTH_NAME_MAX], oldp[AUTH_NAME_MAX], newp[AUTH_NAME_MAX];
            if(!user_str((const char*)a1,name,sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a2,oldp,sizeof(oldp))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a3,newp,sizeof(newp))) return 0xFFFFFFFFFFFFFFFFULL;
            if(strlen(newp)<4) return 0;
            auth_user_t *u=auth_find(name);
            if(!u) return 0;
            auth_user_t *cur=login_result_user();
            int is_self = (cur==u);
            if(!is_self && !syscall_cap(CAP_USER_ADMIN)) return 0;   /* so muda a propria */
            if(is_self) return auth_change_password(name, oldp, newp) ? 1 : 0;
            auth_user_t *target=auth_find(name);
            if(!target) return 0;
            return auth_change_password_admin(name, newp) ? 1 : 0;
        }
        case SYS_AUTH_CREATE_USER: {     /* admin: (name,pass) -> cria voluntario */
            if(!syscall_cap(CAP_USER_ADMIN)) return 0xFFFFFFFFFFFFFFFFULL;
            char name[AUTH_NAME_MAX], pass[AUTH_NAME_MAX];
            if(!user_str((const char*)a1,name,sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a2,pass,sizeof(pass))) return 0xFFFFFFFFFFFFFFFFULL;
            if(strlen(name)<3 || strlen(pass)<4) return 0;
            if(auth_find(name)) return 0;
            return auth_create_user(name, pass, CAP_FB_DRAW|CAP_AISTHESIS|CAP_FS_READ|CAP_IDIOS_WRITE, false) ? 1 : 0;
        }
        case SYS_CONTRACT_LIST: {
            if(!a1 || !a2) return 0xFFFFFFFFFFFFFFFFULL;
            size_t bsz = (size_t)a2; if(bsz > 4096) bsz=4096;
            char tmp[4096]; tmp[0]=0; size_t used=0;
            int n=synallagma_count();
            for(int i=0;i<n;i++){
                contract_t *c=synallagma_contract_at(i);
                if(!c || !c->ativo) continue;
                char b[FS_NAME_MAX+72];
                snprintf(b,sizeof(b),"  - %s em %s (r=%d w=%d x=%d)\n",
                    c->nome[0]?c->nome:"(sem nome)", c->caminho,
                    c->leitura?1:0, c->escrita?1:0, c->execucao?1:0);
                size_t bl=strlen(b);
                if(used + bl + 1 >= bsz) break;
                memcpy(tmp+used, b, bl+1); used+=bl;
            }
            size_t out = used+1; if(out > bsz) out=bsz;
            if(!user_ptr_ok((const void*)a1, out)) return 0xFFFFFFFFFFFFFFFFULL;
            memcpy((void*)a1, tmp, out);
            return 0;
        }
        case SYS_SESSION_LOGIN: {
            char name[AUTH_NAME_MAX];
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            /* ABI 1.5: a3=tok de sessao (emitido pelo servico 'auth'). Com
               ticket valido o kernel NÃO consulta o store (o authd o possui);
               monta a sessao a partir dos caps registrados no ticket. a3=0
               mantem o caminho classico (store do kernel / login ring-0). */
            auth_user_t *u=0;
            if(a3){
                u=auth_ticket_consume(name, a3);
                if(!u) return 0;
            } else {
                u=auth_find(name);
                if(!u || !u->active) return 0;
            }
            if(a2){                                  /* aceitou os contratos */
                int n=synallagma_count();
                for(int i=0;i<n;i++){
                    contract_t *c=synallagma_contract_at(i);
                    if(c && c->ativo) consent_grant(i);
                }
            } else {
                /* recusou: nao ha sessao */
                return 0;
            }
            login_set_user(u);
            /* Sessao ativa: o processo herda as capabilities da conta logada
               (admin -> CAP_ALL), e descendentes via SYS_EXEC herdarao do pai. */
            if(current_proc) current_proc->rights |= u->caps;
            return 1;
        }
        case SYS_AUTH_TICKET_ISSUE: {   /* ABI 1.5: servico 'auth' -> token de sessao */
            if(!syscall_cap(CAP_USER_ADMIN)){ kprint("[arkhe] SYS_AUTH_TICKET_ISSUE: sem CAP_USER_ADMIN\n"); return 0xFFFFFFFFFFFFFFFFULL; }
            char name[AUTH_NAME_MAX];
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            uint64_t tok=0;
            if(!auth_ticket_issue(name, a2, &tok)) return 0xFFFFFFFFFFFFFFFFULL;
            if(a3){
                if(!user_ptr_ok((const void*)a3, sizeof(uint64_t))) return 0xFFFFFFFFFFFFFFFFULL;
                *(uint64_t*)a3=tok;
            }
            return 0;
        }
        case SYS_AUTH_HASH: {       /* ABI 1.5: hashing computacional p/ authd */
            if(!syscall_cap(CAP_USER_ADMIN)) return 0xFFFFFFFFFFFFFFFFULL;
            char pw[64], salt[33], out[65];
            if(!user_str((const char*)a1, pw, sizeof(pw))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_str((const char*)a2, salt, sizeof(salt))) return 0xFFFFFFFFFFFFFFFFULL;
            if(!a3 || !user_ptr_ok((const void*)a3, sizeof(out))) return 0xFFFFFFFFFFFFFFFFULL;
            /* mesmo caminho protegido do login classico: PBKDF2 roda com IRQs
               desligadas (bug de preempcao de ring3 em computo longo). */
            auth_hash_password(pw, salt, out);
            memcpy((void*)a3, out, sizeof(out));
            return 0;
        }
        case SYS_CONSOLE: {
            if(a1==0){ fb_console_clear(); }
            else if(a1==1){ reboot(); }
            else if(a1==2){ poweroff(); }
            return 0;
        }
        case SYS_WAITPID: {
            if(!a1) return 0xFFFFFFFFFFFFFFFFULL;
            int code = proc_waitpid((uint32_t)a1);
            return code < 0 ? 0xFFFFFFFFFFFFFFFFULL : (uint64_t)(uint32_t)code;
        }
        case SYS_SVC_REGISTER: {
            if(!syscall_cap(CAP_IPC)) return 0xFFFFFFFFFFFFFFFFULL;
            char name[32];
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            return svc_register(name)==0 ? 0 : 0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_SVC_QUERY: {
            if(!a1) return 0xFFFFFFFFFFFFFFFFULL;
            char name[32];
            if(!user_str((const char*)a1, name, sizeof(name))) return 0xFFFFFFFFFFFFFFFFULL;
            uint32_t pid=0;
            if(svc_query(name, &pid)!=0) return 0xFFFFFFFFFFFFFFFFULL;
            if(a2){
                if(!user_ptr_ok((const void*)a2, sizeof(uint32_t))) return 0xFFFFFFFFFFFFFFFFULL;
                *(uint32_t*)a2 = pid;
            }
            return pid;
        }
        case SYS_FB_DRAW: {          /* a1=cmd (fb_cmd_t*), CAP_FB_DRAW */
            if(!syscall_cap(CAP_FB_DRAW)) return 0xFFFFFFFFFFFFFFFFULL;
            if(!a1) return 0xFFFFFFFFFFFFFFFFULL;
            if(!user_ptr_ok((const void*)a1, sizeof(fb_cmd_t))) return 0xFFFFFFFFFFFFFFFFULL;
            fb_cmd_t cmd;
            memcpy(&cmd,(const void*)a1,sizeof(cmd));
            char textbuf[FB_CMD_TEXT_MAX+1];
            if(cmd.op==FB_CMD_TEXT || cmd.op==FB_CMD_CHAR){
                if(cmd.op==FB_CMD_CHAR) return 0;
                if(!cmd.text) return 0xFFFFFFFFFFFFFFFFULL;
                if(!user_str(cmd.text, textbuf, sizeof(textbuf))) return 0xFFFFFFFFFFFFFFFFULL;
            }
            switch(cmd.op){
                case FB_CMD_CLEAR: fb_clear(cmd.color); return 0;
                case FB_CMD_RECT:  fb_draw_rect(cmd.x,cmd.y,cmd.w,cmd.h,cmd.color); return 0;
                case FB_CMD_PIXEL: fb_draw_rect(cmd.x,cmd.y,1,1,cmd.color); return 0;
                case FB_CMD_TEXT:  fb_draw_text(cmd.x,cmd.y,textbuf,cmd.color,cmd.color2); return 0;
                case FB_CMD_CHAR:  fb_draw_char(cmd.x,cmd.y,(char)cmd.w,cmd.color,cmd.color2); return 0;
            }
            return 0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_IO_PORT: {          /* a1=op(0in8..2in32,3out8..5out32), a2=port, a3=val */
            if(!syscall_cap(CAP_DEV_IO)) return 0xFFFFFFFFFFFFFFFFULL;
            uint16_t port=(uint16_t)a2;
            if((port==0x64 || port==0x60) && (uint32_t)a1<=2){
                if(port==0x60){
                    uint8_t v=inb_port(0x60);
                    return (uint64_t)v;
                }
            }
            switch((uint32_t)a1){
                case 0:  return (uint64_t)inb_port(port);
                case 1:  return (uint64_t)inw_port(port);
                case 2:  return (uint64_t)inl_port(port);
                case 3:  outb_port(port,(uint8_t)a3); return 0;
                case 4:  outw_port(port,(uint16_t)a3); return 0;
                case 5:  outl_port(port,(uint32_t)a3); return 0;
            }
            return 0xFFFFFFFFFFFFFFFFULL;
        }
        case SYS_MMAP: {          /* a1=paginas -> base VA do heap (RW|NX), ABI 1.3 */
            process_t *p = current_proc;
            if(!p || !p->is_user) return 0xFFFFFFFFFFFFFFFFULL;
            size_t pages=(size_t)a1;
            if(pages==0) pages=1;
            if(pages > 8192) return 0xFFFFFFFFFFFFFFFFULL;   /* <=32MB por chamada */
            int slot=p->umap_heap;
            int new_slot = (slot<0);
            uint64_t base;
            if(slot<0){
                if(p->umap_count>=8) return 0xFFFFFFFFFFFFFFFFULL;
                uint64_t top=0;
                for(int i=0;i<p->umap_count;i++){
                    uint64_t e=p->umap_base[i]+p->umap_size[i]; if(e>top) top=e;
                }
                slot=p->umap_count++;
                base=(top+0xFFF)&~(uint64_t)0xFFF;
                base += 0x10000;                    /* guarda apos o topo (stack) */
                p->umap_base[slot]=base;
                p->umap_size[slot]=0;
                p->umap_heap=slot;
            } else {
                base=p->umap_base[slot]+p->umap_size[slot];
            }
            uint64_t mapped=base;
            for(size_t i=0;i<pages;i++){
                uint8_t *phys=pmm_alloc(1);
                if(!phys || !paging_map_user(p->ctx.cr3, p->pid, mapped, (uintptr_t)phys,
                                             0x7|(1ULL<<63))){   /* P|US|W|NX */
                    if(phys) pmm_free(phys, 1);
                    /* rollback total: desmapeia o que ja foi mapeado e devolve
                       os frames ao PMM (nada de PTE pendurada) */
                    for(uint64_t va=base;va<mapped;va+=0x1000){
                        uint64_t f=paging_unmap_user(p->ctx.cr3, va);
                        if(f) pmm_free((void*)(uintptr_t)f, 1);
                    }
                    if(new_slot){
                        p->umap_count--;        /* restaura o slot */
                        p->umap_heap=-1;
                    }
                    return 0xFFFFFFFFFFFFFFFFULL;
                }
                mapped += 0x1000;
            }
            p->umap_size[slot] = (mapped - p->umap_base[slot]);
            return base;
        }
        case SYS_DEV_MAP: {          /* ABI 1.4: a1=phys, a2=size -> VA (driver MMIO) */
            process_t *p=current_proc;
            if(!p || !p->is_user) return 0xFFFFFFFFFFFFFFFFULL;
            if(!syscall_cap(CAP_DEV_IO)) return 0xFFFFFFFFFFFFFFFFULL;
            uint64_t phys=a1, size=a2;
            if((phys & 0xFFF) || size==0 || size > 0x100000) return 0xFFFFFFFFFFFFFFFFULL;
            size=(size+0xFFF)&~(uint64_t)0xFFF;
            if(!p->umap_devio_base)
                p->umap_devio_base=USER_DEVIO_BASE;
            uint64_t va=p->umap_devio_base + p->umap_devio_used;
            uint64_t done=0;
            for(;done<size;done+=0x1000){
                if(!paging_map_user(p->ctx.cr3, p->pid, va+done, phys+done,
                                    0x7|(1ULL<<63)|PTE_AVAIL_MMIO)){   /* P|US|W|NX|MMIO */
                    return 0xFFFFFFFFFFFFFFFFULL;
                }
            }
            p->umap_devio_used += size;
            return va;
        }
        case SYS_V2P: {              /* ABI 1.4: a1=va -> PA (DMA do driver) */
            process_t *p=current_proc;
            if(!p || !p->is_user) return 0xFFFFFFFFFFFFFFFFULL;
            if(!syscall_cap(CAP_DEV_IO)) return 0xFFFFFFFFFFFFFFFFULL;
            uint64_t pa=paging_phys_of_user(p->ctx.cr3, a1, 0);
            return pa ? pa : 0xFFFFFFFFFFFFFFFFULL;
        }
        default:
            if(nr < SYSCALL_NAME_MAX) {
                char b[80]; snprintf(b,80,"[arkhe] syscall desconhecida: %llu\n",(unsigned long long)nr);
                kprint(b);
            }
            return 0xFFFFFFFFFFFFFFFFULL;
    }
}