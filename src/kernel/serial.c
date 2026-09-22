#include "thais.h"
static inline void outb(uint16_t port,uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

void serial_init(void){
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80); outb(0x3F8+0,0x01); outb(0x3F8+1,0x00);
    outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7); outb(0x3F8+4,0x0B);
}
void serial_write(const char *s){ while(*s){ while(!(inb(0x3F8+5)&0x20)); outb(0x3F8,*s++); } }
void kprint(const char *s){
    serial_write(s);
    /* espelho de boot: visibilidade dos logs do kernel na tela (GOP) em
       hardware real, ativo ate o login assumir (fb_console_init). */
    if(fb_console_boot_active()) fb_console_write(s);
}
