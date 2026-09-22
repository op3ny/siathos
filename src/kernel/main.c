#include "thais.h"

const char *boot_phrases[PHRASE_COUNT] = {
    "\"Eles correm sem distancia, o olho alheio a sombra, alheio ao pe, alheio ao chao, nao identificam, delimitam\" - Baleia (Atlas)",
    "\"A liberdade exige oposicao a toda forma de coercao contra individuos pacificos.\" - Thais",
    "Uma ode a Menger, Hayek, Rothbard e Mises.",
    "\"A menor minoria na Terra e o individuo...\" - Ayn Rand (A Virtude do Egoismo)"
};

/* declara helpes fornecidos em outros arquivos */
void init_start(thais_fb_t *fb,bool use_fb);
#ifdef THAIS_TEST_APPS
void init_spawn_test_apps(void);
#endif
#if defined(THAIS_TEST_RING3)
void init_spawn_ring3_demo(void);
#endif

uint64_t boot_kernel_phys = 0;

void thais_main(thais_boot_info_t *bi){
    serial_init();
    boot_kernel_phys = (bi && bi->magic==THAIS_BOOT_MAGIC) ? bi->kernel_phys : 0;
    kprint("[arkhe] Siaht OS iniciando... Made with love by Thais (op3n/op3ny)\n");
    thais_fb_t fb={0};
    bool have_fb=false;
    if(bi && bi->magic==THAIS_BOOT_MAGIC && bi->fb_addr){
        fb.addr=bi->fb_addr; fb.width=bi->fb_width; fb.height=bi->fb_height; fb.pitch=bi->fb_pitch; fb.bpp=bi->fb_bpp;
        fb_init(&fb);
        kprint("[aisthesis] framebuffer GOP ativo\n");
        splash_show(&fb);
        for(int p=0;p<=100;p++){
            splash_set_progress(p);
            splash_set_spinner(p%4);
            delay_busy(150000);
        }
        /* consola de boot na tela (logs do kernel visiveis em hardware real) */
        fb_console_activate();
        have_fb=true;
    } else {
        kprint("[aisthesis] sem framebuffer, modo serial\n");
    }

    /* memoria */
    pmm_init(bi);            kprint("[init] pmm ok\n");
    heap_init();             kprint("[init] heap ok\n");
    /* memoria virtual (compartilhada nesta fase) */
    paging_init();           kprint("[init] paging ok\n");
    /* GDT/TSS */
    gdt_init();              kprint("[init] gdt ok\n");
    /* processos + capabilities + scheduler */
    proc_init();             kprint("[init] proc ok\n");
    cap_init();              kprint("[init] cap ok\n");
    sched_init();            kprint("[init] sched ok\n");
    /* filesystem + contratos */
    fs_init();               kprint("[init] fs ok\n");
    fs_init_defaults();      kprint("[init] fs defaults ok\n");
    auth_init();             kprint("[init] auth ok\n");
    synallagma_init();       kprint("[init] synallagma ok\n");
    synallagma_load_contracts();
    /* IPC */
    ipc_init();              kprint("[init] ipc ok\n");
    svc_init();              kprint("[init] svc ok\n");
    /* syscalls */
    syscall_init();          kprint("[init] syscall ok\n");
    /* interrupcoes: IDT + PIC + PIT */
    idt_init();              kprint("[init] idt ok\n");
    idt_load();              kprint("[init] idt loaded\n");
    pic_remap();             kprint("[init] pic ok\n");
    pit_init(100);           kprint("[init] pit ok\n");
    keyboard_init();         kprint("[init] teclado ok\n");
    kprint("[kinesis] processos prontos\n");
    kprint("[synallagma] contratos voluntarios carregados\n");

    /* Fase M: o processo init (PID 1) cuida da sessao inteira:
       login -> consentimento -> shell. A autenticacao/FS ainda sao servicos
       de kernel embutidos (migracao p/ isolamento real em Fase P, com ring3
       + paging por processo). O scheduler ja roda: login e preemptivel. */
    init_start(have_fb?&fb:0, have_fb);
#ifdef THAIS_TEST_APPS
    /* processos de demonstracao (apenas no build de teste): escalonamento + IPC */
    init_spawn_test_apps();
#endif
#ifdef THAIS_TEST_RING3
    /* demo de anel 3 (Fase K): processo de usuario executando via enter_user */
    // init_spawn_ring3_demo();
    /* app ring 3 'fetch' agora e spawnado via apps_try_run em init_spawn_test_apps
       (testapp.c), usando exatamente o mesmo caminho que o shell 'praxia'. */
#endif

    /* liga as interrupcoes e inicia o scheduler */
    kprint("[kinesis] iniciando agendamento (preemptivo)\n");
    enable_interrupts();
    sched_run_first();
    halt();
}
