/* teste host: compila odigos_pliktrologiou.c com stub spud e alimenta
   scancodes set-1 (a/t/h/s/e/n), verificando o decode. */
#include <stdio.h>
#include <string.h>

static unsigned char q[512];
static int qh=0, qt=0;
static unsigned char st=0x1c;

static unsigned char my_inb(unsigned short port){
    if(port==0x64){ return qh<qt?0x1d:0x1c; }
    if(port==0x60){ if(qh<qt){unsigned char v=q[qh++]; if(qh>=qt) st=0x1c; return v;} return 0; }
    return 0;
}
static void my_outb(unsigned short port, unsigned char val){
    if(port==0x64){ st |= (val==0x60||val==0xAE)?0x08:0x00; }
    if(port==0x60){ if(st&0x08){ st&=~0x08; } else { q[qt++]=0xfa; } }
}
static void my_write(const char *b){ printf("%s", b); }

#define spud_inb my_inb
#define spud_outb my_outb
#define spud_write my_write
#define spud_sys(...) 0
#define spud_svc_serve dummy_serve
#define SYS_EXIT 1
#define SYS_GETPID 1
#define IPC_TYPE_KBD_GET 1

static int dummy_serve(const char *n, int (*h)(uint32_t,uint32_t,const char*,char*,size_t)){
    char resp[96];
    const char *seq[]={"a","t","h","s","e","n","q",0};
    for(int i=0;seq[i];i++){
        const char *k=seq[i];
        for(int r=0; r<1; r++){
            q[qt++]=(k[0]=='q')?0x10: /* q = 0x10 */ 0x1e;
        }
        size_t rm=sizeof resp;
        int n=h(0,IPC_TYPE_KBD_GET,"",resp,rm);
        if(n>0) printf("HOST-DEC result key %s -> 0x%02x '%c'\n", k, (unsigned char)resp[0], resp[0]);
        else printf("HOST-DEC key %s -> <none>\n", k);
        /* consome o resto dos breaks (O X) */
    }
    return 0;
}

#undef main
#define main odigos_main
#include "odigos_pliktrologiou.c"
#undef main

int main(void){
    return odigos_main(0,0);
}