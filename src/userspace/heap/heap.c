/* heap - valida malloc/free userspace via SYS_MMAP (ABI 1.3).
   Imprime linhas deterministas p/ a regressao (runtest grepa '[heap] malloc ok'). */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    (void)argc; (void)argv;
    char *a=(char*)spud_malloc(100);
    if(!a){
        spud_write("[heap] malloc falhou\n");
        spud_sys(SYS_EXIT,1,0,0,0,0);
        return 1;
    }
    for(int i=0;i<99;i++) a[i]=(char)('a'+(i%26));
    a[99]=0;
    spud_write("[heap] malloc ok bytes=");
    spud_print_dec(100);
    spud_write(" conteudo='");
    spud_write(a);
    spud_write("'\n");
    char *b=(char*)spud_malloc(20000);          /* forca 2o crescimento do heap */
    if(!b){
        spud_write("[heap] malloc2 falhou\n");
        spud_sys(SYS_EXIT,2,0,0,0,0);
        return 2;
    }
    for(int i=0;i<19999;i++) b[i]=(char)('0'+(i%10));
    b[19999]=0;
    spud_write("[heap] malloc2 ok len=");
    spud_print_dec(19999);
    spud_write("\n");
    spud_free(a);
    spud_write("[heap] free ok\n");
    spud_sys(SYS_EXIT,0,0,0,0,0);
    return 0;
}