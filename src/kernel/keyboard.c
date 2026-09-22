#include "thais.h"
#include <stdbool.h>

// Teclado PS/2 - US Internacional com dead keys (', ", `, ~, ^) - mapeamento corrigido zxcvbnm
// (usa outb_port/inb_port de util.c; sem duplicacao de I/O helpers)

// TODO: adicionar suporte USB HID keyboard (usb.c / hid.c). Por ora o init
// apenas inicializa o core USB e imprime mensagem; a leitura de entrada
// continua por portas PS/2 (inb_port 0x60).

// scancode set 1 correto: 0x2B=\, 0x2C=z, 0x2D=x, 0x2E=c, 0x2F=v, 0x30=b, 0x31=n, 0x32=m, 0x33=,, 0x34=., 0x35=/
static const char sc1[128]={
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0x08,0,
    'q','w','e','r','t','y','u','i','o','p','[',']',0x0D,0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,
    '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0
};
static const char sc1_shift[128]={
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0x08,0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}',0x0D,0,
    'A','S','D','F','G','H','J','K','L',':','"', '~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0
};

static bool shift=false, capslock=false, ctrl=false;
static char dead=0;
static char queued=0;
static bool usb_kbd_present = false;

static bool is_dead(char c){ return c=='\'' || c=='"' || c=='`' || c=='~' || c=='^'; }

void keyboard_init(void){
    outb_port(0x64,0xAE);
    while(inb_port(0x64)&1) inb_port(0x60);
    /* config 0x47 (traducao set-1 + IRQ kbd/aux + interfaces) e scancode
       set 2; a leitura vem traduzida p/ set 1 (decoder sc1). */
    outb_port(0x64,0x60);
    outb_port(0x60,0x47);
    outb_port(0x60,0xF0);
    outb_port(0x60,0x02);
    while(inb_port(0x64)&1) inb_port(0x60);
    kprint("[aisthesis] teclado PS/2 pronto (US intl)\n");

    /* inicializar USB: xHCI real (se houver) */
    usb_init();
    if (usb_enumerate_hid_keyboard())
    {
        kprint("[usb] teclado HID registrado.\n");
        usb_kbd_present = true;
        /* sem xHCI real (modo dev) mantem a IRQ simulada p/ testes */
        if (!xhci_kbd_present_p())
            irq_install_handler(12, usb_irq_handler);
    }
    else
        kprint("[usb] teclado HID nao encontrado; usando PS/2.\n");
}

static bool kbd_poll(uint8_t *scan){
    if(inb_port(0x64)&1){
        *scan=inb_port(0x60); return true;
    }
    return false;
}

bool keyboard_has_key(void){ return (inb_port(0x64)&1)!=0; /* apenas consulta, nao consome */ }

/* Processa um scancode PS/2 (set 1) e retorna o caractere (0 = consumido,
   sem caractere printavel). Mantem shift/caps/ctrl/dead/queued. */
static char ps2_process(uint8_t scan){
    if(scan==0xE0){
        uint8_t s2;
        for(int i=0;i<1000;i++){ if(kbd_poll(&s2)) break; __asm__ volatile("pause"); }
        return 0;
    }
    if(scan&0x80){
        uint8_t up=scan&0x7F;
        if(up==0x2A||up==0x36) shift=false;
        if(up==0x1D) ctrl=false;
        return 0;
    }
    if(scan==0x2A||scan==0x36){ shift=true; return 0; }
    if(scan==0x1D){ ctrl=true; return 0; }
    if(scan==0x3A){ capslock=!capslock; return 0; }
    char c;
    if(scan==0x56) c = shift ? '?' : '/';
    else if(scan==0x73) c = shift ? '?' : '/';
    else {
        const char *map = shift ? sc1_shift : sc1;
        c=map[scan];
        if(c==0) return 0;
        if(capslock && ((c>='a'&&c<='z')||(c>='A'&&c<='Z'))) c = (c>='a'&&c<='z') ? (c-'a'+'A') : (c-'A'+'a');
    }
    if(is_dead(c) && dead==0){
        dead=c;
        return 0;
    }
    if(dead){
        char d=dead; dead=0;
        if(c==' '){
            return d;
        }
        if((d=='\''||d=='`'||d=='"'||d=='~'||d=='^') && ((c>='a'&&c<='z')||(c>='A'&&c<='Z'))){
            queued=c;
            return d;
        }
        queued=c;
        return d;
    }
    return c;
}

char keyboard_getc(void){
    /* teclado USB (xHCI) primeiro: se houver report novo, entrega; senao
       cai no PS/2. Ambas as fontes sao servidas para o teclado funcionar em
       QEMU (o monitor so injeta via PS/2) e em hardware real (USB nativo). */
    for(;;){
        char c;
        if(usb_kbd_present && usb_kbd_getc(&c)==0){
            if(c){ dead=0; queued=0; return c; }
            continue;               /* report novo sem caractere: engole */
        }
        if(queued){ c=queued; queued=0; return c; }
        uint8_t scan;
        if(kbd_poll(&scan)){
            c=ps2_process(scan);
            if(c) return c;
        }
        __asm__ volatile("pause");
    }
}

char keyboard_getc_nonblock(bool *has){
    if(queued){ *has=true; char c=queued; queued=0; {
        char db[40]; snprintf(db,40,"[rd] queued c=%d\n",(int)(unsigned char)c); kprint(db);
        return c;
    }}
    char c;
    if(usb_kbd_present && usb_kbd_getc(&c)==0 && c){
        *has=true; dead=0; queued=0; {
        char db[40]; snprintf(db,40,"[rd] usb c=%d\n",(int)(unsigned char)c); kprint(db);
        return c;
    }}
    uint8_t s;
    if(kbd_poll(&s)){
        c=ps2_process(s);
        if(c){ *has=true; {
        char db[40]; snprintf(db,40,"[rd] ps2 c=%d\n",(int)(unsigned char)c); kprint(db);
        return c;
    }}
    }
    *has=false; return 0;
}

void keyboard_get_line(char *buf, size_t max, bool echo, bool mask){
    size_t i=0;
    while(i<max-1){
        char c=keyboard_getc();
        if(c==0) continue;
        if(c==0x0D){
            buf[i]=0;
            if(echo){ serial_write("\r\n"); fb_console_write("\r\n"); }
            dead=0; queued=0;
            return;
        }
        if(c==0x08){
            if(i>0){
                i--;
                if(echo){ serial_write("\b \b"); fb_console_write("\b \b"); }
            }
            continue;
        }
        if(c<0x20) continue;
        if((unsigned char)c>126) c='?';
        buf[i++]=c;
        if(echo){
            char e[2]; e[0]= mask?'*':c; e[1]=0;
            serial_write(e); fb_console_write(e);
        }
    }
    buf[max-1]=0;
    if(echo){ serial_write("\r\n"); fb_console_write("\r\n"); }
}
