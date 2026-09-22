#include "thais.h"
#include <stdint.h>
#include <stdbool.h>

/* pci.c — barramento PCI minimo (bus 0): leitura de config space via portas
   0xCF8/0xCFC e busca pelo controlador xHCI (classe 0x0C, subclasse 0x03,
   prog-if 0x30 = USB3 xHCI). O MMIO do BAR0 e acessado pelo kernel via mapa
   identidade (endereco fisico dereferenciado direto). */

typedef struct {
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if;
    uint64_t bar0;               /* BAR0 como endereco MMIO (bits [31:4]<<4) */
} pci_dev_t;

static void pci_read_dword(uint8_t bus, uint8_t dev, uint8_t fn,
                           uint8_t reg, uint32_t *val){
    uint32_t addr = 0x80000000UL | ((uint32_t)bus<<16) | ((uint32_t)dev<<11)
                  | ((uint32_t)fn<<8) | (reg & 0xFC);
    outl_port(0xCF8, addr);
    *val = inl_port(0xCFC);
}

static int pci_find_xhci(pci_dev_t *out){
    for(int dev=0; dev<32; dev++){
        for(int fn=0; fn<8; fn++){
            uint32_t vid;
            pci_read_dword(0, dev, fn, 0, &vid);
            if((vid & 0xFFFF) == 0xFFFF) break;      /* slot vazio */
            if((vid & 0xFFFF) == 0x0000) break;
            uint32_t cc;
            pci_read_dword(0, dev, fn, 0x08, &cc);
            uint8_t cls=(cc>>24)&0xFF, sub=(cc>>16)&0xFF, prog=(cc>>8)&0xFF;
            if(cls==0x0C && sub==0x03 && prog==0x30){
                uint32_t bar0;
                pci_read_dword(0, dev, fn, 0x10, &bar0);
                uint64_t base64 = bar0 & 0xFFFFFFF0ULL;
                if(bar0 & 0x4){                /* BAR 64-bit: dword alto no BAR1 */
                    uint32_t bar1;
                    pci_read_dword(0, dev, fn, 0x14, &bar1);
                    base64 |= ((uint64_t)bar1 << 32);
                }
                out->vendor = vid & 0xFFFF;
                out->device = (vid>>16) & 0xFFFF;
                out->class_code = cls; out->subclass = sub; out->prog_if = prog;
                out->bar0 = base64;
                return 1;
            }
            if(fn==0){
                uint32_t hdr;
                pci_read_dword(0, dev, fn, 0x0C, &hdr);
                if(!(hdr & 0x80000000)) break;   /* funcao unica: sem fn>0 */
            }
        }
    }
    return 0;
}

/* Expoe o endereco fisico do BAR0 do controlador xHCI (mapa identidade), ou
   0 se nao houver controlador USB3 no barramento. Usado pelo core USB para
   decidir entre modo real (xHCI mapeado) e modo simulacao (reports estaticos). */
uint64_t usb_xhci_bar(void){
    pci_dev_t d;
    if(pci_find_xhci(&d)){
        char b[160];
        snprintf(b,160,"[pci] xHCI encontrado: ven=%04x dev=%04x BAR0=0x%llx\n",
                 d.vendor, d.device, (unsigned long long)d.bar0);
        kprint(b);
        return d.bar0;
    }
    return 0;
}