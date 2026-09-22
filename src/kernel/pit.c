#include "thais.h"

/* pit.c — PIT (Programmable Interval Timer) para o tick do scheduler.
   Canal 0, modo 2 (rate generator), gera IRQ0 (vector 32 apos remap). */

void pit_init(uint32_t hz){
    if(hz==0) hz=100;
    uint32_t divisor = 1193182UL / hz;
    outb_port(0x43, 0x36);                 /* canal 0, lobyte/hibyte, modo 3 */
    outb_port(0x40, (uint8_t)(divisor & 0xFF));
    outb_port(0x40, (uint8_t)((divisor>>8) & 0xFF));
    kprint("[aisthesis] PIT timer configurado\n");
}

/* Leitura do contador (latch) do canal 0 do PIT: devolve o valor atual de
   16 bits do down-counter a 1.193182 MHz. Usado para medicao de tempo fine
   (resolucao ~0.84us) fora do tick do scheduler. */
uint16_t pit_latch_read(void){
    outb_port(0x43, 0x00);                 /* canal 0, latch */
    uint8_t lo=inb_port(0x40);
    uint8_t hi=inb_port(0x40);
    return (uint16_t)(lo | ((uint16_t)hi<<8));
}

/* mede o intervalo decorrido entre duas leituras de pit_latch_read em
   microsegundos, lidando com o wrap do contador de 16 bits. */
uint32_t pit_us_since(uint16_t latch_prev){
    uint16_t now=pit_latch_read();
    uint16_t elapsed=(uint16_t)(latch_prev - now);   /* down-counter: passou tempo */
    return (uint32_t)(((uint64_t)elapsed * 1000000UL) / 1193182UL);
}
