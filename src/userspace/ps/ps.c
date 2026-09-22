/* ps - lista processos (MARCO 5) - app userspace via spud.h.
   Uso: ps | kinesis. Retorna 0. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    (void)argc;
    spud_procinfo_t pl[64];
    long n=spud_sys(SYS_PROC_LIST, (uint64_t)(uintptr_t)pl, 64, 0, 0, 0);
    if(n<0){ spud_write("ps: erro\n"); return 1; }
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
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}
