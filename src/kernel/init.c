#include "thais.h"
#include <stdbool.h>

/* init.c — primeiro processo (PID 1). Inicia os serviços e o ambiente do
   usuario. Nesta etapa, init propriamente so é o processo que roda a shell;
   os serviços (auth/fs) ainda sao funcoes do kernel (migrados na Fase M). */

static thais_fb_t boot_fb;
static bool has_fb=false;

/* kormi — apps de boot ("formas"/servicos essenciais) iniciados ANTES do
   login via exec_embedded_args (ring 3, bytes embutidos de s_mods[]). Cada
   modulo de kormi e spawnado sem ser aguardado; por heranca de caps
   (init == CAP_ALL) os kormi recebem CAP_DEV_IO / CAP_FB_DRAW / CAP_IPC etc.,
   para implementarem drivers (teclado USB, rede...), servicos e GUI. ABI 1.5:
   o boot nao depende mais do ramfs (exec direto da tabela embutida). */
static void kormi_start(void){
    for(size_t i=0;i<fs_mod_count();i++){
        if(!fs_mod_is_kormi(i)) continue;
        const char *name=fs_mod_name(i);
        uint32_t pid=0;
        int r=exec_embedded_args(name, 0, 0, &pid);
        if(r==FS_OK && pid){
            /* kormi = servicos confiaveis pre-login: drivers (devio/fb),
               e authd precisa emitir tickets de sessao (CAP_USER_ADMIN —
               container de delegacao, exige administracao de usuarios). */
            cap_grant(pid, CAP_DEV_IO|CAP_FB_DRAW|CAP_USER_ADMIN);
            char b[96]; snprintf(b,96,"[init] kormi '%s' em anel 3 (pid %u)\n", name, pid); kprint(b);
        } else {
            char b[96]; snprintf(b,96,"[init] kormi '%s' nao iniciado\n", name); kprint(b);
        }
    }
}

/* init (PID 1) = processo da sessao do usuario. Primeiro autentica
   (login_main faz bootstrap/consentimento), e em seguida roda a shell.
   So encerra quando a shell encerrar. Este e o "init" classico: um
   processo pequeno que orquestra o ambiente do usuario. */
static void session_entry(void){
    thais_fb_t *fb = has_fb ? &boot_fb : 0;
    /* MARCO 2: a sessao agora roda em RING 3 como o app 'spoudazo'
       (login + consentimento + shell). init (ring 0) apenas prepara o
       console, spawna o app via exec_elf e o respawna quando ele sair
       (logout) — o loop de login volta a mostrar. Se o binario faltar,
       cai no login/shell ring 0 (fallback classico). */
    fb_console_init(fb);
    fb_console_clear();
    /* iniciar apps de boot antes do login (drivers/servicos essenciais) */
    kormi_start();
    for(;;){
        uint32_t pid=0;
        /* ABI 1.5: sessao roda direto dos bytes embutidos (sem fs_read) */
        int r=exec_embedded_args("spoudazo", 0, 0, &pid);
        if(r!=FS_OK || !pid){
            kprint("[init] spoudazo ausente; sessao ring 0 (fallback)\n");
            login_main(fb);
            thais_sh_main(fb);
            break;
        }
        char b[80]; snprintf(b,80,"[init] sessao ring 3 'spoudazo' pid %u\n", pid); kprint(b);
        /* aguarda a sessao encerrar (logout) para reapresentar o login */
        for(;;){
            process_t *t=proc_get(pid);
            if(!t || !t->present) break;
            sched_yield();
        }
        kprint("[init] sessao encerrada; novo login\n");
    }
    proc_exit(0);
}

void init_start(thais_fb_t *fb, bool use_fb){
    if(fb && use_fb){ boot_fb=*fb; has_fb=true; }
    kprint("[init] PID 1: iniciando sessao (login -> shell)\n");
    /* cria o processo init = a sessao do usuario (auth + shell) */
    process_t init;
    proc_create("init", CAP_ALL, session_entry, &init);
}
