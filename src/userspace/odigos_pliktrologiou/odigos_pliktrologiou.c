/* odigos_pliktrologiou — driver de teclado em RING 3 (arquitetura-alvo:
   drivers reais como /kormi). Le 0x60/0x64 via SYS_IO_PORT (CAP_DEV_IO),
   decodifica scancodes set 1 (US intl, dead keys) e expoe o teclado como o
   servico IPC 'kbd'. O kernel (SYS_READ) consome a tecla via ipc_send/try-
   receive quando o servico existe. Cobre o teclado embutido (8042/EC set 1)
   e, para teclados somente-USB, o caminho legacy do firmware (UEFI Legacy
   Support emula o teclado USB atras das portas PS/2), detectado em
   kbd_identify(). EHCI/XHCI nativo = gap futuro (skill).
   NOTA (bugfix 2/3 workaround): commit da tecla em memoria (kbd_pending)
   antes de qualquer syscall — o retorno de syscall deste kernel nao preserva
   registradores callee-saved de forma confiavel; depender de registro entre
   o drain e o reply zerava o byte. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

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

static int shift=0, capslock=0, ctrl=0;
static char dead=0, queued=0;
static volatile char kbd_hit=0, kbd_pending=0;

static int is_dead(char c){
    return c=='\'' || c=='"' || c=='`' || c=='~' || c=='^';
}

static char kbd_drain_to_char(void){
    kbd_hit=0;
    for(int n=0;n<64;n++){
        if(!(spud_inb(0x64)&1)) return 0;
        uint8_t scan=spud_inb(0x60);
        if(scan==0xE0){
            for(int i=0;i<8;i++){
                if(spud_inb(0x64)&1){ (void)spud_inb(0x60); break; }
            }
            continue;
        }
        if(scan&0x80){
            uint8_t up=scan&0x7F;
            if(up==0x2A||up==0x36) shift=0;
            if(up==0x1D) ctrl=0;
            continue;
        }
        if(scan==0x2A||scan==0x36){ shift=1; continue; }
        if(scan==0x1D){ ctrl=1; continue; }
        if(scan==0x3A){ capslock=!capslock; continue; }
        char c;
        if(scan==0x56 || scan==0x73) c=shift?'?':'/';
        else {
            const char *map=shift?sc1_shift:sc1;
            c=map[scan & 0x7F];
            if(c==0) continue;
            if(capslock && ((c>='a'&&c<='z')||(c>='A'&&c<='Z')))
                c=(c>='a'&&c<='z')?(char)(c-'a'+'A'):(char)(c-'A'+'a');
        }
        if(is_dead(c) && dead==0){ dead=c; continue; }
        if(dead){
            char d=dead; dead=0;
            if(c==' '){ kbd_hit=1; kbd_pending=d; return d; }
            if((d=='\''||d=='`'||d=='"'||d=='~'||d=='^') &&
               ((c>='a'&&c<='z')||(c>='A'&&c<='Z'))){ queued=c; kbd_hit=1; kbd_pending=d; return d; }
            queued=c;
            kbd_hit=1; kbd_pending=d;
            return d;
        }
        if(queued){ char q=queued; queued=0; kbd_hit=1; kbd_pending=q; return q; }
        kbd_hit=1; kbd_pending=c;
        return c;
    }
    return 0;
}

static int kbd_handler(uint32_t from, uint32_t type,
                       const char *req, char *resp, size_t resp_max){
    (void)from; (void)req;
    if(type==IPC_TYPE_KBD_GET){
        (void)kbd_drain_to_char();
        if(resp_max>=1){ resp[0]=kbd_hit?kbd_pending:0; return 1; }
        return 0;
    }
    if(resp_max>=1){ resp[0]='?'; return 1; }
    return 0;
}

/* Identifica a topologia do teclado conectado:
     0 = 8042/EC (PS/2 nativo ou teclado USB roteado pelo firmware legacy-USB)
     1 = somente teclado USB detectado (sem indice de 8042) — requer pilha
         USB (EHCI/XHCI) futura; o driver ainda o serve via legacy-USB.
   Heuristica: 8042 presente => bit de "tecla pendente"/cmd de 0x64 responde
   a um read (D7 comprime), e o histograma de 0x64 nunca congela em 0xFF
   (sem controladora). Simples e determinista para o build atual. */
static int kbd_identify(void){
    uint8_t ctrl=spud_inb(0x64);
    int ps2=0;
    for(int i=0;i<3;i++){
        uint8_t v=spud_inb(0x64);
        if(v!=0xFF && v!=(ctrl&0xE0)) ps2++;
    }
    return ps2>0 ? 0 : 1;
}

static void kbd_report(const char *via){
    char b[96]="[odigos] teclado conectado: ";
    size_t n=18;
    const char *c=via;
    while(*c && n<90){ b[n++]=*c++; }
    b[n++]='\n'; b[n]=0;
    spud_write(b);
}

static void kbd_reset_8042(void){
    for(int i=0;i<16;i++){
        if(spud_inb(0x64)&1){ (void)spud_inb(0x60); }
        else break;
    }
    spud_outb(0x64,0xAA);
    for(int i=0;i<200;i++){
        if(spud_inb(0x64)&1){ (void)spud_inb(0x60); break; }
    }
    spud_outb(0x64,0xAE);
    /* configura o 8042 (byte de config via cmd 0x60): traducao set-1 ativa,
       IRQs de teclado/aux ligados, interfaces habilitadas (0x47). Sem isso o
       QEMU/placa nao sinaliza OBF para os dados do teclado. */
    spud_outb(0x64,0x60);
    spud_outb(0x60,0x47);
    spud_outb(0x60,0xF4);           /* habilita varredura do dispositivo */
    spud_outb(0x60,0xF0);
    spud_outb(0x60,0x02);           /* scancode set 2 (xlate entrega set-1) */
    for(int i=0;i<64;i++){
        if(spud_inb(0x64)&1){ (void)spud_inb(0x60); }
    }
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    long pid=spud_sys(SYS_GETPID, 0, 0, 0, 0, 0);
    (void)pid;
    kbd_reset_8042();
    if(kbd_identify()==1)
        kbd_report("teclado USB (via pilha futura)");
    else
        kbd_report("PS/2 (8042/EC; USB via legacy do firmware)");
    spud_svc_serve("kbd", kbd_handler);
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 0;
}