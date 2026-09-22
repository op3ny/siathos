#include "thais.h"
#include "ring3demo_img.h"
#include <stdbool.h>

/* ring3demo.c — processo de DEMONSTRACAO em RING 3 (Fase K).
   Compilado SOMENTE quando THAIS_TEST_RING3 e definido (make TEST=1 RING3=1).
   Usa proc_create_user para criar um processo que executa um programinha
   embutido (ring3demo.asm) no espaco de enderecos proprio, em anel 3, via
   enter_user/user_trampoline. Se a entrada + preempeção + retorno ring3
   funcionarem, o serial imprime "RING3-OK" e o processo fica em loop de
   SYS_YIELD (cedendo CPU) sem gerar page fault. */

#define RING3_USER_VADDR 0x200000000000u   /* mesmo user_vaddr usado por proc_create_user */

#ifdef THAIS_TEST_RING3

void init_spawn_ring3_demo(void){
    process_t r3;
    int pid = proc_create_user("ring3-demo", CAP_ALL,
                               RING3_USER_VADDR,
                               (void*)ring3demo_img, RING3DEMO_IMG_SIZE,
                               &r3);
    if(pid < 0){
        kprint("[ring3] falha ao criar processo ring 3\n");
        return;
    }
    kprint("[ring3] processo ring 3 criado (pid pronto)\n");
}

#else
void init_spawn_ring3_demo(void){ }
#endif
