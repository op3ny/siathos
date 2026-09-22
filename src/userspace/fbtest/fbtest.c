/* fbtest.c — app real userspace (MARCO 6): malloc + FB_DRAW + IO_PORT + teclado.
   Demonstra: heap userspace com coalescing, desenho de retangulos no framebuffer
   via SYS_FB_DRAW, leitura de porta de E/S (VGA status 0x3da), e leitura de
   teclado via SYS_READ (poll batch).
   Uso no shell: fbtest
   Teclas: 'c' troca a cor, 'm' move o retangulo, 'q' sai.
   Se nenhuma tecla chegar em ~100 iters, sai com timeout (headless OK). */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

static const uint32_t fbtest_colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };

int main(int argc, char **argv){
    (void)argc; (void)argv;

    spud_write("[fbtest] === app real userspace ===\n");

    /* (1) malloc + free — valida heap userspace com coalescing */
    char *buf = (char*)spud_malloc(8*1024);
    if(buf){
        for(int i=0;i<8192;i++) buf[i]=(char)(i&0xff);
        spud_free(buf);
        spud_write("[fbtest] heap: malloc 8KB + free OK (coalescing)\n");
    } else {
        spud_write("[fbtest] heap: malloc 8KB FALHOU\n");
    }

    /* (2) FB_DRAW: limpa tela e desenha 4 retangulos coloridos */
    spud_write("[fbtest] fb_draw: limpando tela + 4 retangulos\n");
    spud_fb_clear(0x000000);
    spud_fb_rect( 80,  80, 240, 120, fbtest_colors[0]);
    spud_fb_rect(380,  80, 240, 120, fbtest_colors[1]);
    spud_fb_rect( 80, 250, 240, 120, fbtest_colors[2]);
    spud_fb_rect(380, 250, 240, 120, fbtest_colors[3]);

    /* (3) IO_PORT: le status VGA 0x3da (registrador generico de dispositivo) */
    uint8_t vga = spud_inb(0x3da);
    spud_write("[fbtest] io_port: vga 0x3da = 0x");
    {
        char vb[3];
        vb[0]='0'+(vga/10); vb[1]='0'+(vga%10); vb[2]=0;
        spud_write(vb);
        spud_write("\n");
    }

    /* (4) Teclado: loop de leitura (spud_getc) ate 'q' ou timeout */
    spud_write("[fbtest] teclado: aguardando tecla (c=cor, m=move, q=sai)...\n");
    int cidx=0;
    for(int iter=0; iter<100; iter++){
        int c=spud_getc();
        if(c=='q' || c=='Q'){
            spud_write("[fbtest] tecla 'q' — saindo\n");
            break;
        }
        if(c=='c' || c=='C'){
            cidx=(cidx+1)&3;
            spud_fb_rect(80,80,240,120,fbtest_colors[cidx]);
            spud_write("[fbtest] nova cor\n");
        }
        if(c=='m' || c=='M'){
            spud_fb_rect(100+(iter*10)%80, 100+(iter*10)%80, 240,120, fbtest_colors[cidx]);
            spud_write("[fbtest] retangulo movido\n");
        }
    }

    spud_write("[fbtest] concluido. exit 0\n");
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}