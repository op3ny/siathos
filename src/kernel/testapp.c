#include "thais.h"
#include <stdbool.h>

/* testapp.c — processos de DEMONSTRACAO/TESTE (escalonamento + IPC + apps ring 3).
   Compilados SOMENTE quando THAIS_TEST_APPS e definido (make TEST=1).
   Com o make comum (limpo), este arquivo nao contribui com nada:
   o sistema sobe apenas com o init+shell, sem apps de teste. */

#ifdef THAIS_TEST_APPS

static void demo_exec(void);

/* appctl — processo RING 0 que roda os apps ring 3 embutidos de forma
   SERIALIZADA (um por vez, aguardando cada um terminar), exatamente como o
   shell 'praxia' faz no uso interativo. Isso evita o cenario (ainda nao
   consolidado no kernel) de multiplos processos ring 3 vivos ao mesmo tempo,
   que corrompe o heap/#UD. Cada app roda isolado em seu proprio anel 3. */

static bool app_done(uint32_t pid){
    process_t *p = proc_get(pid);
    return !p || !p->present;
}

static void appctl(void){
    uint32_t p=0;

    kprint("\n[appctl] executa /praxis/fetch\n");
    int r=apps_try_run("/praxis/fetch", &p);
    { char b[96]; snprintf(b,96,"[appctl] fetch spawn r=%d pid=%u; aguardando…\n", r, p); kprint(b); }
    if(r==1 && p){ while(!app_done(p)) sched_yield(); }
    kprint("[appctl] fetch terminou\n");

    const char *ea[]={"/praxis/echo","Ola","mundo","from","ring3"};
    kprint("\n[appctl] executa /praxis/echo Ola mundo from ring3\n");
    r=apps_try_run_args("/praxis/echo", 5, ea, &p);
    { char b[96]; snprintf(b,96,"[appctl] echo spawn r=%d pid=%u; aguardando…\n", r, p); kprint(b); }
    if(r==1 && p){ while(!app_done(p)) sched_yield(); }
    kprint("[appctl] echo terminou\n");

    kprint("\n[appctl] executa /praxis/ps\n");
    r=apps_try_run("/praxis/ps", &p);
    { char b[96]; snprintf(b,96,"[appctl] ps spawn r=%d pid=%u; aguardando…\n", r, p); kprint(b); }
    if(r==1 && p){ while(!app_done(p)) sched_yield(); }
    kprint("[appctl] ps terminou\n");

    const char *lsterr[]={"/praxis/ls","/synallagma"};
    kprint("\n[appctl] executa /praxis/ls /synallagma\n");
    r=apps_try_run_args("/praxis/ls", 2, lsterr, &p);
    { char b[96]; snprintf(b,96,"[appctl] ls spawn r=%d pid=%u; aguardando…\n", r, p); kprint(b); }
    if(r==1 && p){ while(!app_done(p)) sched_yield(); }
    kprint("[appctl] ls terminou\n");

    const char *caterr[]={"/praxis/cat","/nomos/manifesto.txt"};
    kprint("\n[appctl] executa /praxis/cat /nomos/manifesto.txt\n");
    r=apps_try_run_args("/praxis/cat", 2, caterr, &p);
    { char b[96]; snprintf(b,96,"[appctl] cat spawn r=%d pid=%u; aguardando…\n", r, p); kprint(b); }
    if(r==1 && p){ while(!app_done(p)) sched_yield(); }
    kprint("[appctl] cat terminou\n");

    /* valida o CAMINHO REAL DO SHELL (cmd_praxia -> apps_try_run_args) com
       repasse de args, sem depender do teclado PS/2 (perde teclas). */
    kprint("\n[appctl] shell: 'praxia /praxis/echo ola mundo' (via cmd_praxia)\n");
    {
        char line[128];
        snprintf(line,sizeof(line),"praxia /praxis/echo ola mundo");
        thais_exec_line(line);
    }
    /* aguarda o echo do shell terminar (proc mais novo; procura por nao-presente) */
    {
        uint32_t most_recent=0;
        for(uint32_t pid=1; pid<MAX_PROCS+4; pid++){ process_t *pt=proc_get(pid); if(pt && pt->present && pt->pid>most_recent) most_recent=pt->pid; }
        if(most_recent){ while(!app_done(most_recent)) sched_yield(); }
    }
    kprint("[appctl] echo via shell terminou\n");

    kprint("\n[appctl] todos os apps concluidos\n");
    proc_exit(0);
}

/* ---- STRESS multi-ring3 (diagnostico) ----
   Spawna 3 apps ring 3 CONCORRENTES sem aguardar cada um terminar, para
   reproduzir a limitacao conhecida (multiplos ring-3 vivos -> heap/#UD).
   Espera todos terminarem antes de seguir. Somente no build TEST=1. */
#define STRESS_N 3
static void appctl_stress(void){
    uint32_t pids[STRESS_N];
    const char *av[STRESS_N][3];
    av[0][1]="/praxis/echo"; av[0][2]="AAAA";
    av[1][1]="/praxis/echo"; av[1][2]="BBBB";
    av[2][1]="/praxis/echo"; av[2][2]="CCCC";

    kprint("\n[stress] spawna 3 apps ring 3 CONCORRENTES (echo A/B/C)\n");
    for(int i=0;i<STRESS_N;i++){
        const char *argv[3] = { av[i][1], av[i][2], "/praxis/echo" };
        int r = apps_try_run_args("/praxis/echo", 2, argv, &pids[i]);
        { char b[96]; snprintf(b,96,"[stress] spawn %d r=%d pid=%u\n", i, r, pids[i]); kprint(b); }
    }
    kprint("[stress] aguardando os 3 apps terminarem\n");
    for(;;){
        bool any_left=false;
        for(int i=0;i<STRESS_N;i++){
            if(!app_done(pids[i])){ any_left=true; break; }
        }
        if(!any_left) break;
        sched_yield();
    }
    kprint("[stress] 3 apps concorrentes terminados\n");
    proc_exit(0);
}

void init_spawn_test_stress(void){
    proc_create("stress", CAP_ALL, appctl_stress, 0);
}

/* ---- MARCO 3/5: demo de SYS_EXEC + SYS_WAITPID (exercita o caminho de
   execucao de app externo em /bin, sem depender do teclado PS/2). ---- */
static void demo_exec(void){
    /* MARCO 5/7: apps ELF externos executam e terminam (echo, ls, cat). */
    static const char *paths[] = { "/bin/echo", "/bin/ls" };
    for(int k=0;k<2;k++){
        uint32_t pid=0;
        int r=exec_user_path(paths[k], 0, 0, &pid);
        char b[96];
        if(r!=FS_OK) snprintf(b,sizeof(b),"[demoexec] exec %s falhou (r=%d)\n", paths[k], r);
        else snprintf(b,sizeof(b),"[demoexec] exec %s pid=%u\n", paths[k], pid);
        kprint(b);
        if(r==FS_OK && pid){
            process_t *t=proc_get(pid);
            while(t && t->present && t->state!=PROC_TERMINATED) sched_yield();
        }
    }
    /* MARCO 7: cat /aisthesis/memoria e cat /kinesis/processos (pseudo-FS). */
    {
        static const char *mkv[]={"/bin/cat","/aisthesis/memoria"};
        static const char *kpv[]={"/bin/cat","/kinesis/processos"};
        const char **avs[2]={mkv,kpv};
        for(int k=0;k<2;k++){
            const char **av=avs[k];
            uint32_t pid=0;
            int r=exec_user_path(av[0], 2, av, &pid);
            char b[128];
            if(r==FS_OK && pid){
                process_t *t=proc_get(pid);
                while(t && t->present && t->state!=PROC_TERMINATED) sched_yield();
                snprintf(b,sizeof(b),"[demoexec] cat %s pid=%u ok\n", av[1], pid);
            } else {
                snprintf(b,sizeof(b),"[demoexec] cat %s falhou (r=%d)\n", av[1], r);
            }
            kprint(b);
        }
    }
    /* MARCO 9: sobe os servicos fsd/devd (daemons) e roda o cliente svctest.
       Se os servicos ja estao registrados (kormi), nao duplica — o registro
       tem semantica last-writer-wins e a 2a copia assumiria o nome e quembraria
       a fila de mensagens do servico original. */
    {
        static const char *svcs[]={"/bin/fsd","/bin/devd"};
        for(int k=0;k<2;k++){
            uint32_t existing=0;
            const char *svcname = k==0 ? "fsd" : "devd";
            if(svc_query(svcname, &existing)==0 && existing>0){
                char b[96];
                snprintf(b,sizeof(b),"[demoexec] svc %s ja ativo (kormi pid=%u)\n", svcname, existing);
                kprint(b);
                continue;
            }
            uint32_t pid=0;
            int r=exec_user_path(svcs[k], 0, 0, &pid);
            char b[96];
            if(r!=FS_OK) snprintf(b,sizeof(b),"[demoexec] svc %s falhou (r=%d)\n", svcs[k], r);
            else snprintf(b,sizeof(b),"[demoexec] svc %s pid=%u (daemon)\n", svcs[k], pid);
            kprint(b);
        }
        /* da um respiro para os daemons registrarem o servico antes do cliente */
        for(int i=0;i<16;i++) sched_yield();
        uint32_t pid=0;
        int r=exec_user_path("/bin/svctest", 0, 0, &pid);
        char b[96];
        if(r!=FS_OK) snprintf(b,sizeof(b),"[demoexec] svctest falhou (r=%d)\n", r);
        else snprintf(b,sizeof(b),"[demoexec] svctest pid=%u\n", pid);
        kprint(b);
        if(r==FS_OK && pid){
            process_t *t=proc_get(pid);
            while(t && t->present && t->state!=PROC_TERMINATED) sched_yield();
        }
    }
    /* escrita de arquivos em ring 3 (SYS_FS_WRITE): valida o mesmo wrapper
       que o builtin 'sygraphe/edit' da praxia usa. */
    {
        uint32_t pid=0;
        int r=exec_user_path("/bin/wtest", 0, 0, &pid);
        char b[96];
        if(r!=FS_OK) snprintf(b,sizeof(b),"[demoexec] wtest falhou (r=%d)\n", r);
        else snprintf(b,sizeof(b),"[demoexec] wtest pid=%u\n", pid);
        kprint(b);
        if(r==FS_OK && pid){
            process_t *t=proc_get(pid);
            while(t && t->present && t->state!=PROC_TERMINATED) sched_yield();
        }
    }
    /* isolamento de capacidades (phrourio): app com rights default DEVE ter
       as syscalls de dispositivo negadas (CAP_DEV_IO/CAP_FB_DRAW ausentes). */
    {
        uint32_t pid=0;
        int r=exec_user_path("/bin/phrourio", 0, 0, &pid);
        char b[96];
        if(r!=FS_OK) snprintf(b,sizeof(b),"[demoexec] phrourio falhou (r=%d)\n", r);
        else snprintf(b,sizeof(b),"[demoexec] phrourio pid=%u\n", pid);
        kprint(b);
        if(r==FS_OK && pid){
            process_t *t=proc_get(pid);
            while(t && t->present && t->state!=PROC_TERMINATED) sched_yield();
        }
    }
    proc_exit(0);
}

#ifdef THAIS_TEST_STRESS
/* modo STRESS: apenas o teste de concorrencia roda (sem appctl serial) */
void init_spawn_test_apps(void){
    kprint("[test] modo STRESS: multiplos ring-3 concorrentes\n");
    init_spawn_test_stress();
}
#else
void init_spawn_test_apps(void){
    /* medicao do custo do PBKDF2 (muda com o estado do emulador) */
    {
        static const char *pw="teste123";
        static const char *salthex="0123456789abcdef0123456789abcdef";
        char hexout[65];
        uint16_t t0=pit_latch_read();
        auth_hash_password(pw,salthex,hexout);
        uint16_t t1=pit_latch_read();
        uint32_t us=pit_us_since(t0);
        char tb[96];
        snprintf(tb,sizeof(tb),"[auth] PBKDF2 1x (100000 iters): latch %u->%u = %u us/hash\n",
                 (unsigned)t0,(unsigned)t1,(unsigned)us);
        kprint(tb);
    }
    kprint("[test] spawn appctl (apps ring 3 serializados)\n");
    proc_create("appctl", CAP_ALL, appctl, 0);
    /* MARCO 1: hello oficial — ELF userspace compilado FORA do kernel (gcc),
       embutido em hello_elf.h, executado por exec_elf em anel 3 com argc/argv.
       (valida o pipeline novo sem depender do teclado PS/2) */
    {
        const char *hargs[]={"mundo","bar"};
        uint32_t hpid=0;
        int hr=exec_elf_args("/bin/hello", 2, hargs, &hpid);
        char hb[96]; snprintf(hb,96,"[test] exec_elf_args(/bin/hello) r=%d pid=%u\n", hr, hpid); kprint(hb);
    }
    /* MARCO 3/5: exercita SYS_EXEC + SYS_WAITPID + app externo em /bin.
       Roda no processo appctl (ring 0), que executa e espera o app ring 3. */
    proc_create("demoexec", CAP_ALL, demo_exec, 0);
}
#endif /* THAIS_TEST_STRESS */

#endif /* THAIS_TEST_APPS */
