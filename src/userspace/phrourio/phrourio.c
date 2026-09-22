/* phrourio — valida o isolamento de capacidades (CAP_*) do ring 3.
   Roda via demo_exec no build TEST (pre-login, sem teclado). O processo
   sobe com as rights default de app: CAP_SYSCALL|CAP_FS_READ|CAP_FS_WRITE|
   CAP_EXEC|CAP_IPC. Sem CAP_DEV_IO / CAP_FB_DRAW, as syscalls de dispositivo
   DEVEM ser negadas (retorno -1/0xFF..F) — prova de superfice de ataque
   reduzida (bussola: microkernel, menor privilegio). */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

#define DENY ((long)-1)   /* negado: RAX = 0xFFFFFFFFFFFFFFFF */

static int expect_denied(long r, const char *what){
    if(r==DENY) return 1;
    spud_write("[phrourio] NAO negado: ");
    spud_write(what);
    spud_write("\n");
    return 0;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    int ok=1;

    /* controle positivo 1: leitura de arquivo funciona (CAP_FS_READ default);
       via servico 'fsd' (svc-first) com fallback syscall — conteudo de
       /nomos/manifesto.txt vem da copia local do fsd. */
    char buf[8];
    long r=spud_fs_read("/nomos/manifesto.txt", buf, sizeof(buf));
    if(r<0){ spud_write("[phrourio] controle+ FS_READ falhou\n"); ok=0; }

    /* controle positivo 2: heap fundamental (SYS_MMAP) nao exige cap */
    char *p=(char*)spud_malloc(64);
    if(!p){ spud_write("[phrourio] controle+ malloc falhou\n"); ok=0; }
    else { p[0]='X'; spud_free(p); }

    /* negados por default (sem CAP_DEV_IO): */
    if(!expect_denied(spud_sys(SYS_IO_PORT,0,0x40,0,0,0),"SYS_IO_PORT")) ok=0;
    if(!expect_denied(spud_sys(SYS_DEV_MAP,0,4096,0,0,0),"SYS_DEV_MAP")) ok=0;
    if(!expect_denied(spud_sys(SYS_V2P,(uint64_t)(uintptr_t)p,0,0,0,0),"SYS_V2P")) ok=0;

    /* negado por default (sem CAP_FB_DRAW): */
    spud_fb_t c={SPUD_FB_CLEAR,0,0,0,0,0,0,0};
    if(!expect_denied(spud_sys(SYS_FB_DRAW,(uint64_t)(uintptr_t)&c,0,0,0,0),"SYS_FB_DRAW")) ok=0;

    spud_write(ok ? "[phrourio] capacidades isoladas OK\n"
                  : "[phrourio] isolamento FALHOU\n");
    spud_sys(SYS_EXIT, ok?0:1, 0,0,0,0);
    return ok?0:1;
}