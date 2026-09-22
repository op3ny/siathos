#include "thais.h"
#include <stdbool.h>

static thais_fb_t *g_fb;
static auth_user_t *g_user;
static char cwd[FS_NAME_MAX]="/arkhe";

static void sh_prompt(void){
    char p[96];
    const char *u = g_user?g_user->name:"user";
    snprintf(p,96,"%s@T!%s$ ", u, cwd);
    fb_console_write(p); serial_write(p);
}

static void cmd_list_dir(const char *path){
    char hdr[64]; snprintf(hdr,64,"horasis %s:\n", path?path:cwd);
    fb_console_write(hdr); serial_write(hdr);
    char buf[4096]; buf[0]=0;
    if(vfs_list(cwd, path?path:cwd, buf, sizeof(buf))!=FS_OK){
        fb_console_write("  permissao negada ou caminho invalido\n"); return;
    }
    fb_console_write(buf); serial_write(buf);
}
static void cmd_metabasis(const char *arg){
    if(!arg||!*arg){ fb_console_write("metabasis: uso: metabasis <caminho>  (cd /, cd ..)\n"); return; }
    char norm[FS_NAME_MAX]; if(vfs_normalize(cwd, arg, norm, FS_NAME_MAX)!=FS_OK){ fb_console_write("metabasis: caminho invalido\n"); return; }
    size_t l=strlen(norm); if(l>1 && norm[l-1]=='/') norm[l-1]=0;
    if(fs_is_dir(norm)){ strncpy(cwd,norm,FS_NAME_MAX); char b[64]; snprintf(b,64,"-> %s\n",cwd); fb_console_write(b); }
    else { char b[80]; snprintf(b,80,"metabasis: %s nao e diretorio\n",norm); fb_console_write(b); }
}
static void cmd_ktisis(const char *arg){
    if(!arg||!*arg){ fb_console_write("ktisis: uso: ktisis <nome> [-d]  ex: ktisis nota.txt | ktisis -d pasta\n"); return; }
    bool dir=false; const char *a=arg;
    if(strncmp(arg,"-d ",3)==0){ dir=true; a=arg+3; }
    else if(strncmp(arg,"-d",2)==0 && arg[2]==' '){ dir=true; a=arg+3; }
    int ok=vfs_create(cwd, a, dir?FS_TYPE_DIR:FS_TYPE_FILE);
    char b[80]; snprintf(b,80, ok==FS_OK?"criado %s\n":"falha ao criar (ja existe, sem permissao ou pai inexistente)\n", a); fb_console_write(b);
}
static void cmd_rm(const char *arg){
    if(!arg||!*arg){ fb_console_write("rm: uso: rm <arquivo|dir>\n"); return; }
    int ok=vfs_remove(cwd, arg);
    fb_console_write(ok==FS_OK?"removido\n":"rm: falhou (nao existe, sem permissao ou dir nao vazio)\n");
}
static void cmd_cp(const char *arg){
    if(!arg||!*arg){ fb_console_write("cp: uso: cp <origem> <destino>\n"); return; }
    char tmp[200]; strncpy(tmp,arg,200); tmp[199]=0;
    char *sp=strchr(tmp,' '); if(!sp){ fb_console_write("cp: precisa de origem e destino\n"); return; }
    *sp=0; char *a=tmp; char *b=sp+1; while(*b==' ') b++;
    int ok=vfs_copy(cwd, a, b);
    fb_console_write(ok==FS_OK?"copiado\n":"cp: falhou (sem permissao ou nao existe)\n");
}
static void cmd_mv(const char *arg){
    if(!arg||!*arg){ fb_console_write("mv: uso: mv <origem> <destino>\n"); return; }
    char tmp[200]; strncpy(tmp,arg,200); tmp[199]=0;
    char *sp=strchr(tmp,' '); if(!sp){ fb_console_write("mv: precisa de origem e destino\n"); return; }
    *sp=0; char *a=tmp; char *b=sp+1; while(*b==' ') b++;
    int ok=vfs_move(cwd, a, b);
    fb_console_write(ok==FS_OK?"movido\n":"mv: falhou\n");
}
static void cmd_graphe(const char *arg){
    if(!arg||!*arg){ fb_console_write("graphe: uso: graphe <arquivo>  (cat)\n"); return; }
    char buf[2048]; size_t got=0;
    int n=vfs_read(cwd, arg, buf, sizeof(buf)-1, &got);
    if(n!=FS_OK){ char b[80]; snprintf(b,80,"graphe: %s nao encontrado ou sem permissao\n",arg); fb_console_write(b); return; }
    buf[got]=0; fb_console_write(buf); if(got>0 && buf[got-1]!='\n') fb_console_write("\n");
}
static void cmd_edit(const char *arg){
    if(!arg||!*arg){ fb_console_write("edit: uso: edit <arquivo>\n"); return; }
    char full[FS_NAME_MAX]; vfs_normalize(cwd, arg, full, sizeof(full)); edit_file(full);
}
static void cmd_echo(const char *arg){
    if(!arg) arg="";
    fb_console_write(arg); fb_console_write("\n");
}
static void cmd_pwd(const char *arg){
    (void)arg; char b[80]; snprintf(b,80,"%s\n", cwd); fb_console_write(b);
}
static void cmd_whoami(const char *arg){
    (void)arg; const char *u=g_user?g_user->name:"?"; char b[64]; snprintf(b,64,"%s\n",u); fb_console_write(b);
}
static void cmd_passwd(const char *arg){
    (void)arg;
    if(!g_user){ fb_console_write("sem usuario\n"); return; }
    char oldp[AUTH_NAME_MAX], newp[AUTH_NAME_MAX], newp2[AUTH_NAME_MAX];
    fb_console_write("Senha atual: "); keyboard_get_line(oldp,sizeof(oldp),false,true); fb_console_write("\n");
    if(!auth_verify(g_user->name, oldp)){ fb_console_write("incorreta\n"); return; }
    fb_console_write("Nova senha: "); keyboard_get_line(newp,sizeof(newp),false,true); fb_console_write("\n");
    fb_console_write("Confirme: "); keyboard_get_line(newp2,sizeof(newp2),false,true); fb_console_write("\n");
    if(strcmp(newp,newp2)!=0){ fb_console_write("nao conferem\n"); return; }
    if(strlen(newp)<4){ fb_console_write("muito curta\n"); return; }
    if(auth_change_password(g_user->name, oldp, newp)) fb_console_write("alterada\n"); else fb_console_write("falha\n");
}
static void cmd_useradd(const char *arg){
    if(!g_user || !g_user->is_admin){ fb_console_write("apenas soberano\n"); return; }
    if(!arg||!*arg){ fb_console_write("useradd: uso: useradd <nome>\n"); return; }
    char name[AUTH_NAME_MAX]; strncpy(name,arg,AUTH_NAME_MAX); name[AUTH_NAME_MAX-1]=0;
    char *sp=strchr(name,' '); if(sp) *sp=0;
    char pass[AUTH_NAME_MAX];
    fb_console_write("Senha: "); keyboard_get_line(pass,sizeof(pass),false,true); fb_console_write("\n");
    if(strlen(pass)<4){ fb_console_write("curta\n"); return; }
    bool ok=auth_create_user(name, pass, CAP_FB_DRAW|CAP_AISTHESIS|CAP_IDIOS_WRITE, false);
    fb_console_write(ok?"criado\n":"falha\n");
}
static void cmd_praxia(const char *arg){
    if(!arg||!*arg){ fb_console_write("praxia: uso: praxia <arquivo> [args...]\n"); return; }
    char tmp[600]; strncpy(tmp,arg,600); tmp[599]=0;
    /* separa argv do binario (tokeniza por espacos) */
    const char *argv[64]; int argc=0;
    char *p=tmp;
    argv[argc++] = p;                    /* argv[0] = caminho do binario */
    while(*p){
        if(*p==' '){
            *p=0;
            p++;                       /* passa do NUL (bug: faltava este p++) */
            while(*p==' ') p++;
            if(!*p) break;
            if(argc<63) argv[argc++]=p;
        } else {
            p++;
        }
    }
    char full[FS_NAME_MAX]; vfs_normalize(cwd, argv[0], full, sizeof(full));
    /* 1) tenta app embutido RING 3 (fetch, mem, echo, ls, cat...). Repassa os
       tokens argc/argv ao _start do app (argv[0]=caminho do binario) — espaço
       proprio, W^X, syscalls. */
    uint32_t app_pid=0;
    int ar = apps_try_run_args(full, argc, argv, &app_pid);
    if(ar==1){
        char ai[120]; snprintf(ai,120,"  app ring 3 '%s' (pid %u)\n", argv[0], app_pid);
        fb_console_write(ai); serial_write(ai);
        return;
    }
    /* 2) senao executa ELF / script (caminho classico) */
    int r=exec_file_args(full, argc, argv);
    if(r!=0) fb_console_write("praxia: falhou\n");
}
static void cmd_synallagma(const char *arg){
    if(!arg || strcmp(arg,"list")==0){
        fb_console_write("synallagma - contratos:\n");
        synallagma_list_console();
    }
    else if(strcmp(arg,"add")==0 || strncmp(arg,"add ",4)==0){
        fb_console_write("  (adicione um arquivo .pacto em /synallagma)\n");
    }
    else fb_console_write("synallagma: list | add\n");
}
static void cmd_emporion(const char *arg){
    if(!arg || !*arg) fb_console_write("emporion: search\n");
    else if(strcmp(arg,"search")==0) fb_console_write("emporion: [mises-lib] [hayek-net] [rothbard-crypto]\n");
    else { char b[96]; snprintf(b,96,"emporion: '%s' use search\n",arg); fb_console_write(b); }
}
static void cmd_thais(void){
    fb_console_write("\n  Made with love by Thais (op3n/op3ny)\n");
}
static void cmd_auth(void){
    fb_console_write("auth:\n"); auth_list();
}
static void cmd_help(void){
    fb_console_write("\n=== thais-sh help ===\n");
    fb_console_write(" Arquivos:\n");
    fb_console_write("  horasis [dir] (ls)        listar\n");
    fb_console_write("  metabasis <dir> (cd)      mudar dir\n");
    fb_console_write("  ktisis <arq> [-d]         criar\n");
    fb_console_write("  graphe <arq> (cat)        ler\n");
    fb_console_write("  edit <arq>                editar\n");
    fb_console_write("  rm <arq>                  remover\n");
    fb_console_write("  cp <src> <dst>            copiar\n");
    fb_console_write("  mv <src> <dst>            mover\n");
    fb_console_write("  pwd                       dir atual\n");
    fb_console_write("  echo <txt>                imprimir\n");
    fb_console_write(" Sistema:\n");
    fb_console_write("  whoami                    usuario\n");
    fb_console_write("  passwd                    trocar senha\n");
    fb_console_write("  useradd <nome>            criar usuario (admin)\n");
    fb_console_write("  auth                      listar usuarios\n");
    fb_console_write("  praxia <arq>              executar\n");
    fb_console_write("  synallagma list           contratos\n");
    fb_console_write("  kinesis                   processos\n");
    fb_console_write("  clear / reboot / poweroff\n");
    fb_console_write("  help, exit\n");
}

/* exec_line nao-static: exposto para o teste embutido (TEST=1) dirigir o
   caminho real do shell (cmd_praxia -> apps_try_run_args) de forma
   deterministica, sem depender de teclado PS/2 (que perde teclas). */
void thais_exec_line(char *l){
    while(*l==' ') l++;
    if(!*l) return;
    char cmd[32]={0}, arg[200]={0};
    int i=0; while(*l && *l!=' ' && i<31){ cmd[i++]=*l++; }
    const char *a=l; while(*a==' ') a++;
    for(int j=0;j<199 && a[j];j++) arg[j]=a[j];
    int alen=strlen(arg); while(alen>0 && arg[alen-1]==' ') arg[--alen]=0;
    if(strcmp(cmd,"horasis")==0||strcmp(cmd,"ls")==0) cmd_list_dir(alen?arg:0);
    else if(strcmp(cmd,"metabasis")==0||strcmp(cmd,"cd")==0) cmd_metabasis(alen?arg:0);
    else if(strcmp(cmd,"ktisis")==0||strcmp(cmd,"touch")==0||strcmp(cmd,"mkdir")==0) cmd_ktisis(alen?arg:0);
    else if(strcmp(cmd,"rm")==0||strcmp(cmd,"aphairein")==0) cmd_rm(alen?arg:0);
    else if(strcmp(cmd,"cp")==0) cmd_cp(alen?arg:0);
    else if(strcmp(cmd,"mv")==0) cmd_mv(alen?arg:0);
    else if(strcmp(cmd,"graphe")==0||strcmp(cmd,"cat")==0) cmd_graphe(alen?arg:0);
    else if(strcmp(cmd,"edit")==0) cmd_edit(alen?arg:0);
    else if(strcmp(cmd,"echo")==0) cmd_echo(alen?arg:0);
    else if(strcmp(cmd,"pwd")==0) cmd_pwd(alen?arg:0);
    else if(strcmp(cmd,"whoami")==0) cmd_whoami(alen?arg:0);
    else if(strcmp(cmd,"passwd")==0) cmd_passwd(alen?arg:0);
    else if(strcmp(cmd,"useradd")==0) cmd_useradd(alen?arg:0);
    else if(strcmp(cmd,"praxia")==0||strcmp(cmd,"exec")==0||strcmp(cmd,"run")==0) cmd_praxia(alen?arg:0);
    else if(strcmp(cmd,"synallagma")==0) cmd_synallagma(alen?arg:0);
    else if(strcmp(cmd,"emporion")==0) cmd_emporion(alen?arg:0);
    else if(strcmp(cmd,"agora")==0) fb_console_write("agora sync... [ok]\n");
    else if(strcmp(cmd,"kinesis")==0) kinesis_list();
    else if(strcmp(cmd,"aisthesis")==0) fb_console_write("aisthesis: fb0, teclado US intl\n");
    else if(strcmp(cmd,"thais")==0) cmd_thais();
    else if(strcmp(cmd,"op3n")==0) fb_console_write("op3n - discord\n");
    else if(strcmp(cmd,"op3ny")==0) fb_console_write("op3ny - github\n");
    else if(strcmp(cmd,"auth")==0) cmd_auth();
    else if(strcmp(cmd,"help")==0) cmd_help();
    else if(strcmp(cmd,"clear")==0||strcmp(cmd,"cls")==0) fb_console_clear();
    else if(strcmp(cmd,"reboot")==0){ fb_console_write("reboot...\n"); reboot(); }
    else if(strcmp(cmd,"poweroff")==0||strcmp(cmd,"halt")==0||strcmp(cmd,"shutdown")==0){ fb_console_write("poweroff...\n"); poweroff(); }
    else if(strcmp(cmd,"exit")==0||strcmp(cmd,"logout")==0){ fb_console_write("saindo...\n"); login_main(g_fb); g_user=login_result_user(); strncpy(cwd,"/arkhe",FS_NAME_MAX); }
    else { char b[96]; snprintf(b,96,"'%s' nao encontrado. help\n",cmd); fb_console_write(b); }
}

void thais_sh_main(thais_fb_t *fb){
    g_fb=fb;
    g_user=login_result_user();
    if(!fs_is_dir(cwd)) strncpy(cwd,"/",FS_NAME_MAX);
    fb_console_init(fb);
    fb_console_write("\n=== thais-sh (praxis) ===\n");
    fb_console_write("help para ajuda\n");
    char line[256];
    for(;;){
        sh_prompt();
        keyboard_get_line(line,sizeof(line),true,false);
        thais_exec_line(line);
        yield_to_scheduler();
    }
}
