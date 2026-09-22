/* praxia - shell em RING 3 (MARCO 3).
   Executado por spoudazo apos login via SYS_EXEC.
   Builtins: help, clear/cls, echo, pwd, whoami, uptime, sync/synallagma,
             reboot, poweroff/halt, logout/exit.
   Externos: ls/cat/graphe, ps/kinesis, mem/aisthesis, e qualquer ELF
             em /praxis ou /bin. Usa SYS_EXEC + SYS_WAITPID para executar
             e aguardar processos externos. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

#define CWD_MAX 128
static char g_cwd[CWD_MAX]="/praxis";
static const char *g_user=0;

/* ---- base de modulos: manifestos /bin/*.cfg ----
   Cada app em /bin tem um <mod>.cfg (nome grego, alias POSIX, desc).
   help dinamico lista esses apps; o dispatch resolve nomes/aliases. */
#define MAN_MAX 24
#define MAN_SZ  40
#define MAN_DECSZ 96
static char man_file[MAN_MAX][MAN_SZ];   /* nome do arquivo (base) */
static char man_name[MAN_MAX][MAN_SZ];   /* manifest nome (grego) */
static char man_alias[MAN_MAX][MAN_SZ];  /* manifest alias (POSIX) */
static char man_desc[MAN_MAX][MAN_DECSZ];/* manifest desc */
static int man_n=0;
static int builtin_cmd(const char *c);
static void man_load(void);

/* ---- builtins ---- */
static void cmd_help(void){
    spud_write("\n=== thais-sh (praxis, ring 3) ===\n");
    spud_write("  ls [dir] / horasis      listar diretorio\n");
    spud_write("  cat <arq> / graphe      ler arquivo\n");
    spud_write("  edit <arq> / sygraphe   editar/escrever arquivo (linha '.' grava)\n");
    spud_write("  cd [dir] / metabasis    mudar de diretorio\n");
    spud_write("  ktisis <nome> [-d]      criar arquivo/dir\n");
    spud_write("  anairesis <nome>        remover arquivo/dir\n");
    spud_write("  echo <txt>              imprimir texto\n");
    spud_write("  pwd / whoami            dir atual / usuario\n");
    spud_write("  ps / kinesis            processos\n");
    spud_write("  mem / aisthesis         memoria\n");
    spud_write("  uptime                  tempo de sistema\n");
    spud_write("  sync / synallagma       contratos\n");
    spud_write("  users / katastaseis      listar usuarios\n");
    spud_write("  userdel / katachirmismos remover usuario (admin)\n");
    spud_write("  passwd / metavivasi      trocar senha\n");
    spud_write("  useradd / dimiourgia     criar usuario (admin)\n");
    spud_write("  clear / cls             limpar tela\n");
    spud_write("  reboot / poweroff       reiniciar / desligar\n");
    spud_write("  logout / exit           encerrar sessao\n");
    if(man_n){
        spud_write("  apps (manifestos /bin/*.cfg):\n");
        for(int i=0;i<man_n;i++){
            spud_write("    ");
            spud_write(man_alias[i]);
            spud_write(" / ");
            spud_write(man_name[i]);
            if(man_desc[i][0]){ spud_write(" — "); spud_write(man_desc[i]); }
            spud_write("\n");
        }
    }
}

static void cmd_ls(const char *arg){
    char path[CWD_MAX+32];
    spud_resolve(arg, g_cwd, path, sizeof(path));
    char buf[4096];
    long r=spud_fs_list(path, buf, sizeof(buf));
    if(r<0){ spud_write("  permissao negada ou caminho invalido\n"); return; }
    spud_write(buf);
}

static void cmd_cat(const char *arg){
    if(!arg){ spud_write("cat: uso: cat <arquivo>\n"); return; }
    char path[CWD_MAX+32];
    spud_resolve(arg, g_cwd, path, sizeof(path));
    char buf[1024];
    long r=spud_fs_read(path, buf, sizeof(buf)-1);
    if(r<0){ spud_write("cat: nao encontrado ou sem permissao\n"); return; }
    size_t got=(size_t)r; buf[got]=0;
    spud_write(buf);
    if(got==0 || buf[got-1]!='\n') spud_write("\n");
}

/* sygraphe/edit — edita um arquivo em modo texto simples: cada linha lida e
   acrescentada; uma linha com apenas '.' encerra a edicao e grava. Escrita
   via SYS_FS_WRITE (sobrescreve). Fechar com Ctrl-D (tecla 0x04) descarta. */
static void cmd_sygraphe(const char *arg){
    if(!arg || !*arg){ spud_write("sygraphe: uso: sygraphe <arquivo>  (linha '.' grava)\n"); return; }
    char path[CWD_MAX+32];
    spud_resolve(arg, g_cwd, path, sizeof(path));
    char fbuf[4096]; size_t fn=0;
    spud_write("--- sygraphe (linha com '.' grava e sai) ---\n");
    for(;;){
        char line[120];
        spud_readline(line, sizeof(line), 1, 0);
        spud_write("\n");
        if(spud_cmp(line,".")==0) break;
        if(spud_cmp(line,"\x04")==0){ spud_write("abandonado.\n"); return; }
        size_t ln=spud_len(line);
        if(fn+ln+1>=sizeof(fbuf)){ spud_write("limite de 4095 bytes atingido; gravando parcial.\n"); break; }
        size_t i=0; while(i<ln){ fbuf[fn++]=line[i]; i++; }
        fbuf[fn++]='\n';
    }
    long r=spud_fs_write(path, fbuf, fn);
    if(r==0) spud_write("gravado.\n");
    else spud_write("falha ao gravar (permissao ou caminho invalido).\n");
}

/* metabasis / cd — muda o diretorio atual (resolve e valida existencia) */
static void cmd_cd(const char *arg){
    if(!arg || !*arg || spud_cmp(arg,"~")==0){ spud_ncpy(g_cwd, "/praxis", CWD_MAX); return; }
    char path[CWD_MAX+32];
    spud_resolve(arg, g_cwd, path, sizeof(path));
    char buf[64];
    long r=spud_fs_list(path, buf, sizeof(buf));
    if(r>=0 && spud_len(path)<CWD_MAX){ spud_ncpy(g_cwd, path, CWD_MAX); }
    else spud_write("metabasis: diretorio nao encontrado\n");
}

/* ktisis — cria arquivo ou diretorio. force_dir=1 forza diretorio (mkdir). */
static void cmd_ktisis_force(const char *arg, int force_dir){
    if(!arg || !*arg){ spud_write("ktisis: uso: ktisis <nome> [-d] | mkdir <nome>\n"); return; }
    char name[192]; spud_ncpy(name, arg, sizeof(name));
    int isdir=force_dir;
    size_t nl=spud_len(name);
    if(!isdir && nl>=2 && name[nl-2]=='-' && name[nl-1]=='d'){ isdir=1; name[nl-2]=0; }
    if(name[0]==0 && !isdir){ spud_write("ktisis: nome vazio\n"); return; }
    if(name[0]==0) spud_ncpy(name,"novo_diretorio",sizeof(name));
    char path[CWD_MAX+32];
    spud_resolve(name, g_cwd, path, sizeof(path));
    long r = isdir ? spud_fs_create_dir(path) : spud_fs_create_file(path);
    if(r==0) spud_write(isdir?"diretorio criado.\n":"arquivo criado.\n");
    else spud_write("ktisis: falhou (permissao ou nome invalido)\n");
}

/* anairesis — remove arquivo ou diretorio (tenta; se nao, como vazio) */
static void cmd_anairesis(const char *arg){
    if(!arg || !*arg){ spud_write("anairesis: uso: anairesis <nome>\n"); return; }
    char path[CWD_MAX+32];
    spud_resolve(arg, g_cwd, path, sizeof(path));
    long r=spud_fs_remove(path);
    if(r==0) spud_write("removido.\n");
    else spud_write("anairesis: falhou (nao existe ou protegido)\n");
}

static void cmd_ps(void){
    spud_procinfo_t pl[64];
    long n=spud_sys(SYS_PROC_LIST, (uint64_t)(uintptr_t)pl, 64, 0, 0, 0);
    if(n<0){ spud_write("ps: erro\n"); return; }
    static const char *st[]={"criado","pronto","rodando","bloqueado","terminado"};
    for(long i=0;i<n && i<64;i++){
        const char *s = (pl[i].state>=0 && pl[i].state<=4) ? st[pl[i].state] : "?";
        spud_write("  ");
        spud_print_dec(pl[i].pid);
        spud_write(" ");
        spud_write(pl[i].name);
        spud_write(" [");
        spud_write(s);
        spud_write(pl[i].is_user?" u":" k");
        spud_write("]\n");
    }
}

static void cmd_mem(void){
    spud_sysinfo_t si;
    long r=spud_sys(SYS_SYSINFO, (uint64_t)(uintptr_t)&si, 0, 0, 0, 0);
    if(r!=0){ spud_write("mem: erro\n"); return; }
    spud_write("  total: "); spud_print_dec(si.memory_total); spud_write("B\n");
    spud_write("  usado: "); spud_print_dec(si.memory_used);  spud_write("B\n");
    spud_write("  livre: "); spud_print_dec(si.memory_free);  spud_write("B\n");
}

static void cmd_uptime(void){
    long ms=spud_sys(SYS_UPTIME, 0, 0, 0, 0, 0);
    spud_print_dec((uint64_t)ms/1000); spud_write("s de sistema\n");
}

static void cmd_contracts(void){
    char cbuf[2048];
    long r=spud_sys(SYS_CONTRACT_LIST, (uint64_t)(uintptr_t)cbuf, sizeof(cbuf), 0, 0, 0);
    if(r!=0 || !cbuf[0]){ spud_write("nenhum contrato ativo.\n"); return; }
    spud_write("synallagma:\n"); spud_write(cbuf);
}

/* katastaseis / users — lista usuarios (servico 'auth'; fallback SYS_AUTH_LIST) */
static void cmd_users(void){
    char buf[1024];
    long r=spud_auth_list(buf, sizeof(buf));
    if(r!=0){ spud_write("users: erro\n"); return; }
    spud_write("usuarios:\n"); spud_write(buf);
}

/* katachirmismos / userdel — admin (soberano) remove usuario via authd */
static void cmd_userdelete(const char *arg){
    if(!arg || !*arg){ spud_write("userdel: uso: userdel <usuario>\n"); return; }
    long r=spud_auth_delete(arg);
    if(r==1) spud_write("usuario removido.\n");
    else if(r==0) spud_write("userdel: falhou (soberano, protegido ou requer sessao admin)\n");
    else spud_write("userdel: sem permissao de admin\n");
}

/* metavivasi / passwd — troca a propria senha (ou a de outro, se soberano) */
static void cmd_passwdc(const char *arg){
    char who[32]; spud_ncpy(who, g_user?g_user:"", 32);
    if(arg && *arg){ spud_ncpy(who, arg, 32); }
    if(who[0]==0){ spud_write("passwd: quem sou eu?\n"); return; }
    char oldp[64], newp[64];
    spud_write("senha atual: ");
    spud_readline(oldp, sizeof(oldp), 0, 1);
    spud_write("\n");
    spud_write("nova senha: ");
    spud_readline(newp, sizeof(newp), 0, 1);
    spud_write("\n");
    long r=spud_auth_changepw(who, oldp, newp);
    if(r==1) spud_write("senha alterada.\n");
    else spud_write("falhou (senha atual errada, invalida ou sem admin).\n");
}

/* dimiourgia / useradd — admin (soberano) cria voluntario via authd */
static void cmd_useradd(const char *arg){
    if(!arg || !*arg){ spud_write("useradd: uso: useradd <usuario>\n"); return; }
    char pass[64], pass2[64];
    spud_write("senha: ");
    spud_readline(pass, sizeof(pass), 0, 1);
    spud_write("\n");
    spud_write("confirme: ");
    spud_readline(pass2, sizeof(pass2), 0, 1);
    spud_write("\n");
    if(spud_cmp(pass,pass2)!=0){ spud_write("senhas nao conferem.\n"); return; }
    long r=spud_auth_create_user(arg, pass);
    if(r==1) spud_write("usuario criado.\n");
    else if(r==0) spud_write("useradd: falhou (nome curto/senha curta/existe/requer sessao admin)\n");
    else spud_write("useradd: sem permissao de admin\n");
}

/* ---- base de modulos: manifestos /bin/*.cfg ---- */
static int builtin_cmd(const char *c){
    static const char *b[]={ "help","clear","cls","echo","pwd","whoami",
        "ls","horasis","cat","graphe","edit","sygraphe","cd","metabasis","ktisis","touch",
        "mkdir","anairesis","rm","ps","kinesis","mem","aisthesis","uptime",
        "sync","synallagma","users","katastaseis","userdel","katachirmismos",
        "passwd","metavivasi","useradd","dimiourgia","reboot","poweroff",
        "halt","logout","exit", 0};
    for(int i=0;b[i];i++) if(spud_cmp(c,b[i])==0) return 1;
    return 0;
}
/* extrai o valor de uma chave (key=value, linhas) no manifesto lido */
static void man_key(const char *buf, size_t buflen, const char *key,
                    char *out, int outcap){
    out[0]=0;
    size_t kl=spud_len(key);
    size_t i=0;
    while(i<buflen){
        size_t ls=i;
        while(i<buflen && buf[i]!='\n') i++;
        size_t le=i; i++;
        size_t ks=ls;
        while(ks<le && (buf[ks]==' '||buf[ks]=='\t')) ks++;
        if((size_t)(le-ks)>=kl+1 && spud_cmp(buf+ks, key)==0){
            size_t v=ks+kl;
            while(v<le && (buf[v]==' '||buf[v]=='\t'||buf[v]=='='||buf[v]==':')) v++;
            size_t ve=le;
            while(ve>v && (buf[ve-1]==' '||buf[ve-1]=='\t'||buf[ve-1]=='\r')) ve--;
            size_t vl=ve-v;
            if(vl>=(size_t)outcap) vl=(size_t)outcap-1;
            size_t c=0; while(c<vl){ out[c]=buf[v+c]; c++; }
            out[c]=0;
            return;
        }
    }
}
static void man_load(void){
    char list[2048];
    long r=spud_fs_list("/bin", list, sizeof(list));
    if(r<0) return;
    char *p=list;
    while(*p && man_n<MAN_MAX){
        while(*p==' '||*p=='\n'||*p=='\t') p++;
        if(!*p) break;
        char *tag=p;
        while(*p && *p!='\n') p++;
        size_t ll=(size_t)(p-tag); if(ll>255) ll=255;
        char line[256];
        size_t c=0; while(c<ll){ line[c]=tag[c]; c++; } line[c]=0;
        const char *t=line;
        while(*t==' '||*t=='\t') t++;
        if(spud_cmp(t,"<DIR>")==0 || spud_len(t)<5) continue;
        if(spud_cmp(t,"<ARQ>")==0) t+=5;
        while(*t==' ') t++;
        char base[48]; c=0;
        while(t[c] && t[c]!=' ' && t[c]!='\n' && t[c]!='\t' && c<47){ base[c]=t[c]; c++; }
        base[c]=0;
        size_t bl=spud_len(base);
        if(bl<5 || spud_cmp(base+bl-4,".cfg")!=0) continue;
        base[bl-4]=0;
        char path[72];
        spud_ncpy(path,"/bin/",sizeof(path));
        spud_ncat(path,base,sizeof(path));
        spud_ncat(path,".cfg",sizeof(path));
        char buf[512];
        long got=spud_fs_read(path, buf, sizeof(buf)-1);
        if(got<0) continue;
        buf[got]=0;
        char nm[MAN_SZ], al[MAN_SZ], dc[MAN_DECSZ];
        man_key(buf,(size_t)got,"nome",nm,sizeof(nm));
        man_key(buf,(size_t)got,"alias",al,sizeof(al));
        man_key(buf,(size_t)got,"desc",dc,sizeof(dc));
        if(!nm[0] && !al[0]) continue;
        if(!nm[0]) spud_ncpy(nm,base,sizeof(nm));
        if(!al[0]) spud_ncpy(al,base,sizeof(al));
        /* pula apps que sao builtins da shell (para nao duplicar o help) */
        if(builtin_cmd(nm) || builtin_cmd(al)) continue;
        if(spud_cmp(base,"spoudazo")==0 || spud_cmp(base,"praxia")==0) continue;
        spud_ncpy(man_file[man_n],base,sizeof(man_file[0]));
        spud_ncpy(man_name[man_n],nm,sizeof(man_name[0]));
        spud_ncpy(man_alias[man_n],al,sizeof(man_alias[0]));
        spud_ncpy(man_desc[man_n],dc,sizeof(man_desc[0]));
        man_n++;
    }
}
/* procura comando na tabela; retorna indice ou -1 */
static int man_find(const char *cmd){
    for(int i=0;i<man_n;i++){
        if(spud_cmp(cmd,man_name[i])==0 || spud_cmp(cmd,man_alias[i])==0 ||
           spud_cmp(cmd,man_file[i])==0) return i;
    }
    return -1;
}

/* tenta executar como caminho externo; retorna 0 se nao encontrado, -1 se erro */
static int try_exec(const char *cmd, int argc, const char **argv){
    char path[CWD_MAX+32];
    spud_resolve(cmd, g_cwd, path, sizeof(path));
    uint32_t pid=0;
    long r = spud_sys(SYS_EXEC,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uint32_t)argc,
        argv ? (uint64_t)(uintptr_t)argv : 0,
        0, 0);
    if(r>0){
        spud_sys(SYS_WAITPID, (uint64_t)(uint32_t)r, 0, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* ---- shell loop ---- */
static void shell(void){
    spud_write("\n=== thais-sh (praxis, ring 3) ===\n");
    spud_write("help para ajuda\n");
    char line[256];
    for(;;){
        /* prompt: user@T!/cwd$ */
        spud_write(g_user ? g_user : "user");
        spud_write("@T!");
        spud_write(g_cwd);
        spud_write("$ ");
        spud_readline(line, sizeof(line), 1, 0);
        spud_write("\n");
        /* parse: cmd + arg */
        char cmd[32]; char arg[192];
        size_t i=0; while(line[i]==' ') i++;
        size_t j=0; while(line[i] && line[i]!=' ' && j<31){ cmd[j++]=line[i++]; }
        cmd[j]=0;
        while(line[i]==' ') i++;
        size_t k=0; while(line[i] && k<191){ arg[k++]=line[i]; i++; }
        arg[k]=0;
        if(cmd[0]==0) continue;
        /* builtins */
        if(spud_cmp(cmd,"help")==0) cmd_help();
        else if(spud_cmp(cmd,"clear")==0 || spud_cmp(cmd,"cls")==0)
            spud_sys(SYS_CONSOLE, 0, 0, 0, 0, 0);
        else if(spud_cmp(cmd,"echo")==0){ spud_write(arg); spud_write("\n"); }
        else if(spud_cmp(cmd,"pwd")==0){ spud_write(g_cwd); spud_write("\n"); }
        else if(spud_cmp(cmd,"whoami")==0){ spud_write(g_user?g_user:"?"); spud_write("\n"); }
        else if(spud_cmp(cmd,"ls")==0 || spud_cmp(cmd,"horasis")==0) cmd_ls(arg);
        else if(spud_cmp(cmd,"cat")==0 || spud_cmp(cmd,"graphe")==0) cmd_cat(arg);
        else if(spud_cmp(cmd,"edit")==0 || spud_cmp(cmd,"sygraphe")==0) cmd_sygraphe(arg);
        else if(spud_cmp(cmd,"cd")==0 || spud_cmp(cmd,"metabasis")==0) cmd_cd(arg);
        else if(spud_cmp(cmd,"ktisis")==0 || spud_cmp(cmd,"touch")==0) cmd_ktisis_force(arg, 0);
        else if(spud_cmp(cmd,"mkdir")==0) cmd_ktisis_force(arg, 1);
        else if(spud_cmp(cmd,"anairesis")==0 || spud_cmp(cmd,"rm")==0) cmd_anairesis(arg);
        else if(spud_cmp(cmd,"ps")==0 || spud_cmp(cmd,"kinesis")==0) cmd_ps();
        else if(spud_cmp(cmd,"mem")==0 || spud_cmp(cmd,"aisthesis")==0) cmd_mem();
        else if(spud_cmp(cmd,"uptime")==0) cmd_uptime();
        else if(spud_cmp(cmd,"sync")==0 || spud_cmp(cmd,"synallagma")==0) cmd_contracts();
        else if(spud_cmp(cmd,"users")==0 || spud_cmp(cmd,"katastaseis")==0) cmd_users();
        else if(spud_cmp(cmd,"userdel")==0 || spud_cmp(cmd,"katachirmismos")==0) cmd_userdelete(arg);
        else if(spud_cmp(cmd,"passwd")==0 || spud_cmp(cmd,"metavivasi")==0) cmd_passwdc(arg);
        else if(spud_cmp(cmd,"useradd")==0 || spud_cmp(cmd,"dimiourgia")==0) cmd_useradd(arg);
        else if(spud_cmp(cmd,"reboot")==0){
            spud_write("reboot...\n"); spud_sys(SYS_CONSOLE, 1, 0, 0, 0, 0); for(;;){}
        }
        else if(spud_cmp(cmd,"poweroff")==0 || spud_cmp(cmd,"halt")==0){
            spud_write("poweroff...\n"); spud_sys(SYS_CONSOLE, 2, 0, 0, 0, 0); for(;;){}
        }
        else if(spud_cmp(cmd,"logout")==0 || spud_cmp(cmd,"exit")==0){
            spud_write("saindo...\n"); return;
        }
        else {
            /* tenta executar como comando externo */
            /* monta argv[0]=cmd, argv[1..]=arg tokenizado (simplificado) */
            const char *eargv[16];
            int eargc=0;
            eargv[eargc++]=cmd;
            /* tokenizar arg (separar por espacos) */
            char *ap=arg;
            while(*ap && eargc<15){
                while(*ap==' ') *ap++=0;
                if(!*ap) break;
                eargv[eargc++]=ap;
                while(*ap && *ap!=' ') ap++;
            }
            /* resolucao por manifesto: nome grego/alias -> /bin/<arquivo> */
            int mi=man_find(cmd);
            if(mi>=0 && spud_cmp(man_file[mi],cmd)!=0){
                char mp[CWD_MAX+32];
                spud_ncpy(mp,"/bin/",sizeof(mp));
                spud_ncat(mp,man_file[mi],sizeof(mp));
                uint32_t pid=0;
                long r2=spud_sys(SYS_EXEC,(uint64_t)(uintptr_t)mp,
                    (uint64_t)(uint32_t)eargc,(uint64_t)(uintptr_t)eargv,0,0);
                if(r2>0){ spud_sys(SYS_WAITPID,(uint64_t)(uint32_t)r2,0,0,0,0); continue; }
            }
            if(!try_exec(cmd, eargc, eargv)){
                spud_write("'"); spud_write(cmd); spud_write("' nao encontrado.\n");
            }
        }
    }
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    /* argv[0] = username (passado pelo spoudazo via SYS_EXEC) */
    if(argc>0 && argv[0]) g_user=argv[0];
    man_load();
    shell();
    /* shell fez 'exit'/logout: encerra a sessao (codigo 1) p/ o init re-login */
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 1;
}
