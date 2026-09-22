/* ls - lista diretorio (MARCO 5) - app userspace via spud.h.
   Uso: ls [dir] | horasis [dir]. Retorna 0. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    (void)argc;
    const char *path = (argc>1 && argv[1]) ? argv[1] : "/praxis";
    char buf[4096];
    long r=spud_fs_list(path, buf, sizeof(buf));
    if(r<0){ spud_write("ls: permissao negada ou caminho invalido\n"); return 1; }
    spud_write(buf);
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}
