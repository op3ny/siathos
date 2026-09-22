/* echo - imprime argumentos (MARCO 5) - app userspace via spud.h.
   Uso: echo <t...>. Retorna 0. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    for(int i=1;i<argc;i++){
        if(i>1) spud_write_ch(' ');
        spud_write(argv[i]);
    }
    spud_write("\n");
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}
