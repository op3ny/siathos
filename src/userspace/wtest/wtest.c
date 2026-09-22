/* wtest — valida a escrita/leitura de arquivos em ring 3 de forma
   deterministica, sem teclado: via serviço 'fsd' (svc-first, fallback
   syscall). Escreve, le de volta e confere o conteudo. Usa os mesmos
   wrappers spud_fs_write/spud_fs_read do builtin 'sygraphe'/'graphe' da
   praxia e dos apps cat/ls. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    (void)argc; (void)argv;
    /* /idios e o lar do individuo e esta protegido por contrato (consentimento
       no login); o autoteste roda antes do login, entao escreve em /paradosis
       (tradicao/exemplos), que nao exige consentimento para escrita. */
    const char *path="/paradosis/wtest.txt";
    const char *payload="ola escrita 123\nsegunda linha\n";
    long w=spud_fs_write(path, payload, spud_len(payload));
    if(w!=0){ spud_write("[wtest] SYS_FS_WRITE falhou\n"); return 1; }
    char buf[128];
    long r=spud_fs_read(path, buf, sizeof(buf)-1);
    if(r<0){ spud_write("[wtest] SYS_FS_READ falhou\n"); return 1; }
    buf[r]=0;
    if(spud_cmp(buf,payload)!=0){
        spud_write("[wtest] conteudo divergente: '");
        spud_write(buf);
        spud_write("'\n");
        return 1;
    }
    spud_write("[wtest] escrita+leitura ok: '");
    spud_write(buf);
    spud_write("'\n");
    return 0;
}