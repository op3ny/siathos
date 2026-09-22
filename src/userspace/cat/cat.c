/* cat - le arquivo (MARCO 5) - app userspace via spud.h.
   Uso: cat <arquivo> | graphe <arquivo>. Retorna 1 em erro. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    if(argc<2 || !argv[1]){ spud_write("cat: uso: cat <arquivo>\n"); return 1; }
    char buf[1024];
    long r=spud_fs_read(argv[1], buf, sizeof(buf)-1);
    if(r<0){ spud_write("cat: nao encontrado ou sem permissao\n"); return 1; }
    size_t got=(size_t)r; buf[got]=0;
    spud_write(buf);
    if(got==0 || buf[got-1]!='\n') spud_write("\n");
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}
