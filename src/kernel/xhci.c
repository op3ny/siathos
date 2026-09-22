#include "thais.h"
#include <stdint.h>
#include <stdbool.h>

/* xhci.c — driver xHCI (USB3) p/ teclado HID nativo em QEMU.
   MMIO via mapa identidade. Enumeracao: porta -> Enable Slot -> Address Device
   -> descriptors -> Configure Endpoint (interrupt IN) -> polling do transfer
   ring p/ ler report HID (sem ISR proprio; evita a classe de bugs bugfix 2/3). */

typedef struct { uint32_t dw[4]; } xh_trb_t;

static inline uint32_t xh_re32(volatile uint8_t *b, uint32_t off){
    return *(volatile uint32_t*)(b + off);
}
static inline void xh_wr32(volatile uint8_t *b, uint32_t off, uint32_t v){
    *(volatile uint32_t*)(b + off) = v;
}
static inline void xh_wr64_parts(volatile uint8_t *b, uint32_t off, uint64_t v){
    xh_wr32(b, off,     (uint32_t)v);
    xh_wr32(b, off + 4, (uint32_t)(v >> 32));
}
static inline void busy_pause(void){ __asm__ volatile("pause"); }

static void *xh_mpage(void){
    void *p = pmm_alloc(1);
    if(p) memset(p, 0, 4096);
    return p;
}

/* ---- Operacionais ---- */
#define XH_CMD       0x0000
#define XH_STS       0x0004
#define XH_CRCR      0x0018
#define XH_DCBAAP    0x0030
#define XH_CONFIG    0x0038
#define XH_PORT      0x0400
#define XH_CMD_HCRST (1u<<1)
#define XH_CMD_RUN   (1u<<0)
#define XH_STS_HCH   (1u<<0)
#define XH_STS_CNR   (1u<<11)

/* Runtime: interrupter 0 (stride 0x20; IMAN=0x20 base do RTS) */
#define XH_IMAN    0x20
#define XH_IMOD    0x24
#define XH_ERSTSZ  0x28
#define XH_ERSTBA  0x30
#define XH_ERDP    0x38

/* ---- TRB types ---- */
#define TRB_T_NORMAL 1
#define TRB_T_SETUP  2
#define TRB_T_DATA   3
#define TRB_T_STATUS 4
#define TRB_T_LINK   6
#define TRB_T_ENSLC  9
#define TRB_T_ADRDEV 11
#define TRB_T_CFGEP  12
#define EVT_TRANSFER 32
#define EVT_CMDCOMP  33

#define TRB_TCYCLE   0x1
#define TRB_TIOC     0x20
#define TRB_IDT      0x40
#define TRB_ICHN     0x10
#define TRB_TDIR_IN  0x10000
#define TRB_TLINK_TC 0x2

/* ---- Contextos 32B (indice xHCI: slot=0, ep0=1, ep1out=2, ep1in=3) ---- */
#define ICSLOT  0x20
#define ICEP(n) (0x20 + (n)*0x20)
#define SL_ROUTE(v)    ((v)&0xfffff)
#define SL_SPEED(p)    (((p)&0xf)<<20)
#define SL_LASTCTX(n)  (((n)&0x1f)<<27)
#define SL_RHPORT(p)   (((p)&0xff)<<16)
#define EP_INTERVAL(p) (((p)&0xff)<<16)
#define EP_MULT(p)     (((p)&0x3)<<8)
#define EP_TYPE(p)     ((p)<<3)
#define EP_MAXBURST(p) (((p)&0xff)<<8)
#define EP_MAXPKT(p)   (((p)&0xffff)<<16)
#define EP_CERR(p)     (((p)&0x3)<<1)
#define EP_CTRL  4
#define EP_INTIN 7

#define XH_TRBS 16
#define EP1IDX  3    /* context index do EP1-IN (slot=0, ep0=1, ep1out=2, ep1in=3) */
#define DB1IN   3    /* DCI do EP1-IN = doorbell target = 3 */

static volatile uint8_t *xh_mmio=0;
static uint64_t dcbaa_phys=0, cr_phys=0, er_phys=0, erst_phys=0;
static uint64_t ep0_ring=0, ep1_ring=0;
static uint32_t xh_pad=0, xh_dboff=0x1000, xh_rts=0x1000, xh_ports=0;
static int cr_enq=0, cr_ccs=1;
static int er_idx=0, er_ccs=1;
static int e0idx=0, e0ccs=1, e1idx=0, e1ccs=1;
static uint32_t xh_slot=0, xh_portnum=0, xh_speed=0;
static uint64_t xh_devctx=0;
static int xh_kbd=0;
static uint8_t hcfg_mps=8, hcfg_itv=1;

static void xh_wait(uint32_t mask, int set, const char *what){
    for(unsigned i=0;i<10000000;i++){
        uint32_t s=xh_re32(xh_mmio+xh_pad,XH_STS);
        if(((s&mask)!=0)==(set!=0)) return;
        busy_pause();
    }
    char b[96]; snprintf(b,96,"[xhci] timeout: %s\n",what); kprint(b);
}

static volatile uint8_t *xh_rt(void){ return xh_mmio+xh_rts; }
static volatile uint8_t *xh_dbr(void){ return xh_mmio+xh_dboff; }

/* ring generico com Link TRB em [15]; *ccs toggla no wrap e o Link eh
   reescrito com o novo ciclo (senao a HC para no "ring underrun"). */
static void ring_init(uint64_t ring, int *idx, int *ccs){
    xh_trb_t *t=(xh_trb_t*)(uintptr_t)ring;
    for(int i=0;i<XH_TRBS;i++){ t[i].dw[0]=t[i].dw[1]=t[i].dw[2]=t[i].dw[3]=0; }
    t[15].dw[0]=(uint32_t)ring; t[15].dw[1]=(uint32_t)(ring>>32); t[15].dw[2]=0;
    t[15].dw[3]=(TRB_T_LINK<<10)|TRB_TLINK_TC|(uint32_t)*ccs;
    *idx=0;
}
static void advance(uint64_t ring,int *idx,int *ccs){
    (*idx)++;
    if(*idx>=15){
        *idx=0; *ccs^=1;
        xh_trb_t *t=(xh_trb_t*)(uintptr_t)ring;
        t[15].dw[3]=(TRB_T_LINK<<10)|TRB_TLINK_TC|(uint32_t)*ccs;
    }
}

/* ---- Event ring ---- */
typedef struct { uint32_t dw0, dw1, dw2, dw3; } ev_t;
#define EV_TYPE(ev) (((ev)->dw3>>10)&0x3f)
#define EV_CODE(ev) (((ev)->dw2>>24)&0xff)   /* completion code em DW2[31:24] */
#define EV_SLOT(ev) (((ev)->dw3>>24)&0xff)
#define EV_EP(ev)   (((ev)->dw3>>16)&0x1f)   /* DCI */
#define EV_LEN(ev)  ((ev)->dw2&0xffffff)
#define EV_PTR(ev)  ((((uint64_t)(ev)->dw1)<<32)|(ev)->dw0)

/* Varre o event ring UMA vez, sem bloquear: 1 = consumiu um evento,
   0 = nenhum evento novo. */
static int ev_peek(ev_t *ev){
    uint32_t *b=((uint32_t*)(uintptr_t)er_phys)+er_idx*4;
    uint32_t dw3=b[3];
    if((dw3&1)!=(uint32_t)er_ccs) return 0;
    if(ev){
        ev->dw0=b[0]; ev->dw1=b[1]; ev->dw2=b[2]; ev->dw3=dw3;
    }
    er_idx++; if(er_idx>=XH_TRBS){ er_idx=0; er_ccs^=1; }
    xh_wr64_parts(xh_rt(),XH_ERDP,((er_phys&~0x0FULL)+(uint64_t)(er_idx*16))|(uint64_t)er_ccs);
    return 1;
}

/* Le um evento novo (bloqueia). */
static int ev_wait(ev_t *ev){
    for(unsigned i=0;i<100000000;i++){
        ev_t e;
        if(ev_peek(&e)<=0){ busy_pause(); continue; }
        *ev=e; return 1;
    }
    return -1;
}

/* ---- Command ring ---- */
static void cmd_run(uint64_t ptr,uint32_t dw2,unsigned slotid,int type){
    xh_trb_t *cr=(xh_trb_t*)(uintptr_t)cr_phys+cr_enq;
    cr->dw[0]=(uint32_t)ptr; cr->dw[1]=(uint32_t)(ptr>>32);
    cr->dw[2]=dw2;
    cr->dw[3]=(type<<10)|((slotid&0xff)<<24)|(uint32_t)cr_ccs;
    advance(cr_phys,&cr_enq,&cr_ccs);
    xh_wr32(xh_dbr(),0,0);                   /* doorbell Host Command (slot 0) */
}

static int cmd_wait(ev_t *out){
    ev_t ev;
    for(unsigned i=0;i<8;i++){
        if(ev_wait(&ev)<0) return -1;
        if(EV_TYPE(&ev)!=EVT_CMDCOMP) continue;
        if(out) *out=ev;
        return (EV_CODE(&ev)==1)?0:(int)EV_CODE(&ev);
    }
    return -3;
}

/* ---- Portas ---- */
static uint32_t port_sc(int p){ return xh_re32(xh_mmio+xh_pad,XH_PORT+p*0x10); }

static int xhci_find_device_port(void){
    for(int p=0;(uint32_t)p<xh_ports;p++){
        uint32_t s=port_sc(p);
        if(!(s&1u)) continue;                 /* CCS */
        xh_wr32(xh_mmio+xh_pad,XH_PORT+p*0x10,s|(1u<<4)); /* PR */
        int done=0;
        for(unsigned i=0;i<100000000;i++){ if(port_sc(p)&(1u<<9)){done=1;break;} busy_pause(); }
        xh_wr32(xh_mmio+xh_pad,XH_PORT+p*0x10,port_sc(p)&~(1u<<9));
        char b[96];
        snprintf(b,96,"[xhci] porta %d reset%s ok speed=%u\n",p+1,
                 done?"":"(timeout)",(unsigned)((port_sc(p)>>10)&0xf));
        kprint(b);
        return p+1;
    }
    return 0;
}

/* ---- Init do controlador ---- */
static int xhci_reset(void){
    xh_wait(XH_STS_HCH,1,"HCH alto");
    xh_wait(XH_STS_CNR,0,"CNR limpo");
    xh_wr32(xh_mmio+xh_pad,XH_CMD,XH_CMD_HCRST);
    xh_wait(XH_CMD_HCRST,0,"HCRST clear");
    xh_wait(XH_STS_HCH,1,"halt pos reset");
    return 0;
}

static int xhci_ring_debug(const char *tag){
    volatile uint8_t *op=xh_mmio+xh_pad,*rt=xh_rt();
    char b[200];
    snprintf(b,200,"[xdbg %s] DBOFF=0x%x RTS=0x%x CMD=0x%x STS=0x%x "
             "IMAN=0x%x IMOD=0x%x ERSTSZ=0x%x ERSTBA=0x%llx ERDP=0x%llx CRCR=0x%llx\n",
             tag,(unsigned)xh_dboff,(unsigned)xh_rts,
             (unsigned)xh_re32(op,XH_CMD),(unsigned)xh_re32(op,XH_STS),
             (unsigned)xh_re32(rt,XH_IMAN),(unsigned)xh_re32(rt,XH_IMOD),
             (unsigned)xh_re32(rt,XH_ERSTSZ),
             (unsigned long long)(xh_re32(rt,XH_ERSTBA)|((uint64_t)xh_re32(rt,XH_ERSTBA+4)<<32)),
             (unsigned long long)(xh_re32(rt,XH_ERDP)|((uint64_t)xh_re32(rt,XH_ERDP+4)<<32)),
             (unsigned long long)(xh_re32(op,XH_CRCR)|((uint64_t)xh_re32(op,XH_CRCR+4)<<32)));
    kprint(b);
    uint32_t *cr=(uint32_t*)(uintptr_t)cr_phys;
    uint32_t *er=(uint32_t*)(uintptr_t)er_phys;
    snprintf(b,200,"[xdbg %s] cr[0..7]=%08x %08x %08x %08x %08x %08x %08x %08x\n",
             tag,cr[0],cr[1],cr[2],cr[3],cr[4],cr[5],cr[6],cr[7]);
    kprint(b);
    snprintf(b,200,"[xdbg %s] er[0..15]=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
             tag,er[0],er[1],er[2],er[3],er[4],er[5],er[6],er[7],
             er[8],er[9],er[10],er[11],er[12],er[13],er[14],er[15]);
    kprint(b);
    return 0;
}

static int xhci_enable(void){
    volatile uint8_t *op=xh_mmio+xh_pad,*rt=xh_rt();
    ring_init(cr_phys,&cr_enq,&cr_ccs);
    ring_init(ep0_ring,&e0idx,&e0ccs);
    ring_init(ep1_ring,&e1idx,&e1ccs);
    xh_wr64_parts(op,XH_DCBAAP,dcbaa_phys);
    xh_wr64_parts(op,XH_CRCR,(cr_phys&~0x3FULL)|1);  /* +RCS=1 */
    /* event ring do interrupter 0 */
    xh_wr32(rt,XH_IMAN,0);                      /* IE=0: sem IRQ (polling) */
    xh_wr32(rt,XH_IMOD,0);
    xh_wr32(rt,XH_ERSTSZ,1);
    xh_wr64_parts(rt,XH_ERSTBA,erst_phys);
    xh_wr64_parts(rt,XH_ERDP,(er_phys&~0x0FULL)|1);   /* +PCS=1 */
    xh_wr32(op,XH_CMD,xh_re32(op,XH_CMD)|XH_CMD_RUN);
    xh_wait(XH_STS_HCH,0,"running");
    uint32_t hcp1=xh_re32(xh_mmio,0x04);
    xh_ports=(hcp1>>24)&0xFF;
    xh_wr32(op,XH_CONFIG,(uint8_t)(hcp1&0xFF)); /* MaxSlotsEn */
    kprint("[xhci] controlador operacional\n");
    return 0;
}

/* ---- Control transfer no EP0 ---- */
static uint64_t va_to_phys(uintptr_t v){
    if(v >= 0xffffffff80000000ULL) return (uint64_t)v - 0xffffffff80000000ULL + boot_kernel_phys;
    return (uint64_t)v;                                /* identidade */
}
static void ep0_setup(uint8_t bmr,uint8_t breq,uint16_t wv,uint16_t wi,uint16_t wl,
                      void *data,int dirin){
    xh_trb_t *st=(xh_trb_t*)(uintptr_t)ep0_ring+e0idx;
    st->dw[0]=(uint32_t)(bmr|(breq<<8)|(wv<<16));
    st->dw[1]=(uint32_t)(wi|(wl<<16));
    st->dw[2]=8;
    st->dw[3]=(TRB_T_SETUP<<10)|TRB_IDT|(uint32_t)e0ccs;   /* IDT: setup imediato no TRB */
    advance(ep0_ring,&e0idx,&e0ccs);
    if(data&&wl){
        xh_trb_t *dt=(xh_trb_t*)(uintptr_t)ep0_ring+e0idx;
        uint64_t pa=va_to_phys((uintptr_t)data);
        dt->dw[0]=(uint32_t)pa; dt->dw[1]=(uint32_t)(pa>>32);
        dt->dw[2]=wl;
        dt->dw[3]=(TRB_T_DATA<<10)|TRB_ICHN|(dirin?TRB_TDIR_IN:0)|(uint32_t)e0ccs;
        kprint("[xhci] ep0_setup DATA a\n");
        advance(ep0_ring,&e0idx,&e0ccs);
    }
    xh_trb_t *ss=(xh_trb_t*)(uintptr_t)ep0_ring+e0idx;
    ss->dw[0]=0; ss->dw[1]=0; ss->dw[2]=0;
    ss->dw[3]=(TRB_T_STATUS<<10)|TRB_TIOC|(dirin?0:TRB_TDIR_IN)|(uint32_t)e0ccs;
    advance(ep0_ring,&e0idx,&e0ccs);
    xh_wr32(xh_dbr(),(xh_slot&0xff)*4,1);      /* DB EP0 */
}

static long ep0_ctrl(uint8_t bmr,uint8_t breq,uint16_t wv,uint16_t wi,uint16_t wl,
                     void *data,int dirin){
    ep0_setup(bmr,breq,wv,wi,wl,data,dirin);
    ev_t ev;
    for(unsigned i=0;i<16;i++){
        if(ev_wait(&ev)<0) return -1;
        if(EV_TYPE(&ev)!=EVT_TRANSFER||EV_EP(&ev)!=1) continue;
        uint8_t code=(uint8_t)EV_CODE(&ev);
        if(code==5||code==4) return -2;    /* TRB err / stall */
        return (long)wl;
    }
    return -1;
}

/* ---- Address Device / Configure Endpoint ---- */
static uint32_t slot_speed(uint32_t ps){
    switch(ps){ case 1:return 0; case 2:return 1; case 3:return 2; case 4:return 3; }
    return 0;
}
static uint32_t ep0_mps(uint32_t spd){
    if(spd==2) return 64;
    if(spd==3) return 512;
    return 8;
}

static int xhci_address_device(void){
    uint64_t in=(uint64_t)(uintptr_t)xh_mpage();
    if(!in) return -1;
    uint32_t *ic=(uint32_t*)(uintptr_t)in;
    ic[1]=(1u<<0)|(1u<<1);                      /* add slot+ep0 */
    uint32_t *sc=ic+ICSLOT/4;
    sc[0]=SL_ROUTE(0)|SL_SPEED(xh_speed)|SL_LASTCTX(1);
    sc[1]=SL_RHPORT(xh_portnum);
    uint32_t *e0=ic+ICEP(1)/4;
    e0[1]=EP_TYPE(EP_CTRL)|EP_MAXPKT(ep0_mps(xh_speed))|EP_CERR(3);
    e0[2]=(uint32_t)ep0_ring|1; e0[3]=(uint32_t)(ep0_ring>>32);
    char db[200];
    snprintf(db,200,"[xhci] ic(in=%llx dcb[%u]=%llx) ctl=%08x %08x sl=%08x %08x %08x %08x ep0=%08x %08x %08x %08x\n",
             (unsigned long long)in,(unsigned)xh_slot,
             (unsigned long long)((uint64_t*)(uintptr_t)dcbaa_phys)[xh_slot],
             ic[0],ic[1],ic[ICSLOT/4],ic[ICSLOT/4+1],ic[ICSLOT/4+2],ic[ICSLOT/4+3],
             ic[ICEP(1)/4],ic[ICEP(1)/4+1],ic[ICEP(1)/4+2],ic[ICEP(1)/4+3]);
    kprint(db);
    cmd_run(in,0,xh_slot,TRB_T_ADRDEV);
    int r=cmd_wait(0);
    if(r!=0){
        char b[96]; snprintf(b,96,"[xhci] addr device r=%d\n",r); kprint(b);
        xhci_ring_debug("adrv");
        return -2;
    }
    return 0;
}

static int xhci_get_descriptors(void){
    uint8_t devd[18]={0};
    char db[120];
    snprintf(db,120,"[xhci] devd va=%08x%08x pa=%08x%08x\n",
             (unsigned)((uintptr_t)devd>>32),(unsigned)((uintptr_t)devd&0xffffffff),
             (unsigned)(va_to_phys((uintptr_t)devd)>>32),(unsigned)(va_to_phys((uintptr_t)devd)&0xffffffff));
    kprint(db);
    long r=ep0_ctrl(0x80,0x06,0x0100,0,sizeof(devd),devd,1);
    if(r<0) return -1;
    char b[160];
    snprintf(b,160,"[xhci] devdesc r=%d b=%u%u%u%u%u%u%u%u vid=%u%u\n",
             (int)r,
             devd[0],devd[1],devd[2],devd[3],devd[4],devd[5],devd[6],devd[7],
             devd[8],devd[9]);
    kprint(b);
    devd[0]=0xff; devd[1]=0xee; devd[2]=0xdd;
    r=ep0_ctrl(0x80,0x06,0x0100,0,sizeof(devd),devd,1);
    snprintf(b,160,"[xhci] devdesc2 r=%d b=%u%u%u\n",(int)r,devd[0],devd[1],devd[2]);
    kprint(b);
    uint8_t cfg[512]={0};
    r=ep0_ctrl(0x80,0x06,0x0200,0,9,cfg,1);
    if(r<0) return -2;
    uint16_t total=(uint16_t)(cfg[2]|(cfg[3]<<8));
    if(total>(uint16_t)sizeof(cfg)) total=(uint16_t)sizeof(cfg);
    r=ep0_ctrl(0x80,0x06,0x0200,0,total,cfg,1);
    if(r<0) return -3;
    for(int off=0;off+1<(int)total;){
        uint8_t len=cfg[off],typ=cfg[off+1];
        if(!len) break;
        if(typ==0x05){
            uint8_t ea=cfg[off+2],at=cfg[off+3];
            if((ea&0x80)&&(at&3)==3){
                hcfg_mps=(uint8_t)(cfg[off+4]|(cfg[off+5]<<8));
                hcfg_itv=cfg[off+6];
                snprintf(b,96,"[xhci] HID int-IN ep=%02x mps=%u itv=%u\n",
                         (unsigned)ea,(unsigned)hcfg_mps,(unsigned)hcfg_itv);
                kprint(b);
                return 0;
            }
        }
        off+=len;
    }
    kprint("[xhci] sem endpoint HID IN\n");
    return -4;
}

static int xhci_configure_endpoint(void){
    uint64_t in=(uint64_t)(uintptr_t)xh_mpage();
    if(!in) return -1;
    uint32_t *ic=(uint32_t*)(uintptr_t)in;
    ic[1]=(1u<<0)|(1u<<3);              /* add slot + ep1-IN (ep0 ja configurado) */
    uint32_t *sc=ic+ICSLOT/4;
    sc[0]=SL_ROUTE(0)|SL_SPEED(xh_speed)|SL_LASTCTX(2);
    sc[1]=SL_RHPORT(xh_portnum);
    uint32_t *e0=ic+ICEP(1)/4;
    e0[1]=EP_TYPE(EP_CTRL)|EP_MAXPKT(ep0_mps(xh_speed))|EP_CERR(3);
    e0[2]=(uint32_t)ep0_ring|1; e0[3]=(uint32_t)(ep0_ring>>32);
    uint32_t *e1=ic+ICEP(EP1IDX)/4;
    e1[0]=EP_INTERVAL(hcfg_itv);
    e1[1]=EP_TYPE(EP_INTIN)|EP_MAXPKT(hcfg_mps)|EP_CERR(3);
    e1[2]=(uint32_t)ep1_ring|1; e1[3]=(uint32_t)(ep1_ring>>32);
    cmd_run(in,0,xh_slot,TRB_T_CFGEP);
    int r=cmd_wait(0);
    if(r!=0){
        char b[96]; snprintf(b,96,"[xhci] cfg ep r=%d\n",r); kprint(b);
        return -2;
    }
    kprint("[xhci] endpoint HID IN configurado\n");
    return 0;
}

int xhci_enumerate(void){
    int port=xhci_find_device_port();
    if(!port){ kprint("[xhci] nenhum dispositivo\n"); return -4; }
    xh_portnum=(uint32_t)port;
    xh_speed=slot_speed((port_sc(port-1)>>10)&0xf);
    char b[96];
    snprintf(b,96,"[xhci] porta %d speed=%u\n",port,(unsigned)xh_speed);
    kprint(b);
    cmd_run(0,0,0,TRB_T_ENSLC);
    ev_t ev;
    int r=cmd_wait(&ev);
    if(r!=0){
        xhci_ring_debug("enslot");
        kprint("[xhci] enable slot falhou\n");
        return -5;
    }
    xh_slot=EV_SLOT(&ev);
    xh_devctx=(uint64_t)(uintptr_t)xh_mpage();
    if(!xh_devctx) return -6;
    uint64_t *d=(uint64_t*)(uintptr_t)dcbaa_phys;
    for(int i=0;i<256;i++) d[i]=0;
    d[xh_slot]=xh_devctx;
    if(xhci_address_device()!=0) return -7;
    kprint("[xhci] Address Device ok\n");
    if(xhci_get_descriptors()!=0) return -8;
    if(xhci_configure_endpoint()!=0) return -9;
    xh_kbd=1;
    kprint("[xhci] teclado HID reconhecido via xHCI\n");
    return (int)xh_slot;
}

int xhci_init(uint64_t bar){
    if(!bar) return -1;
    /* BARs 64-bit (ex. QEMU coloca xHCI em 0xc000000000) nao estao no mapa
       inicial: mapeia MMIO em identidade antes de tocar no espaco de regs. */
    if(bar >= 0x100000000ULL)
        paging_map_device_identity(bar, 0x400000);
    xh_mmio=(volatile uint8_t*)(uintptr_t)bar;
    xh_pad=xh_re32(xh_mmio,0)&0xFF;
    uint32_t rts=xh_re32(xh_mmio,0x18);
    if(rts) xh_rts=rts&~0x1FULL; else xh_rts=0x1000;
    uint32_t dbo=xh_re32(xh_mmio,0x14);
    if(dbo) xh_dboff=dbo; else xh_dboff=0x1000;
    dcbaa_phys=(uint64_t)(uintptr_t)xh_mpage();
    cr_phys  =(uint64_t)(uintptr_t)xh_mpage();
    er_phys  =(uint64_t)(uintptr_t)xh_mpage();
    erst_phys=(uint64_t)(uintptr_t)xh_mpage();
    ep0_ring =(uint64_t)(uintptr_t)xh_mpage();
    ep1_ring =(uint64_t)(uintptr_t)xh_mpage();
    if(!dcbaa_phys||!cr_phys||!er_phys||!erst_phys||!ep0_ring||!ep1_ring) return -2;
    xh_trb_t *e=(xh_trb_t*)(uintptr_t)erst_phys;
    e[0].dw[0]=(uint32_t)er_phys; e[0].dw[1]=(uint32_t)(er_phys>>32);
    e[0].dw[2]=XH_TRBS;
    kprint("[xhci] aneis alocados\n");
    if(xhci_reset()!=0) return -3;
    if(xhci_enable()!=0) return -4;
    return 0;
}

int xhci_active(void){ return xh_mmio!=0; }
int xhci_kbd_present_p(void){ return xh_kbd; }

/* Polling do EP1-IN: enfileira um TRB e espera o report HID (8 bytes). */
/* Polling do report HID no EP1-IN. Mantem aquecido um unico TD por vez:
   - sem TD pendente  => grava TRB no espaco produtor atual, avanca o anel,
     acende o doorbell e retorna -1 (i.e. "sem report ainda");
   - com TD pendente  => re-acende o doorbell (NAK retry) e varre o event ring
     sem bloquear (ev_peek) a procura do transfer event do nosso TRB.
   Nao re-submete/enfileira um segundo TD enquanto o primeiro nao completou:
   enfileirar de novo sobre um EP ainda NAK ("in flight" na HC) nunca completa. */
static uint8_t ep1_buf[8];
static int ep1_sub_idx=-1;              /* slot do TD pendente, -1 = nenhum */
static uint64_t ep1_sub_trb=0;          /* endereco (pa) do TRB do TD pendente */
int xhci_kbd_read_hid(uint8_t *buf, uint32_t max){
    if(!xh_kbd||!buf) return -1;
    if(ep1_sub_idx<0){
        xh_trb_t *tr=(xh_trb_t*)(uintptr_t)ep1_ring+e1idx;
        memset(ep1_buf,0,sizeof(ep1_buf));
        uint64_t pa=va_to_phys((uintptr_t)ep1_buf);
        tr->dw[0]=(uint32_t)pa; tr->dw[1]=(uint32_t)(pa>>32);
        tr->dw[2]=sizeof(ep1_buf);
        tr->dw[3]=(TRB_T_NORMAL<<10)|TRB_TIOC|(uint32_t)e1ccs;
        ep1_sub_idx=e1idx;
        ep1_sub_trb=ep1_ring+(uint64_t)e1idx*16;
        advance(ep1_ring,&e1idx,&e1ccs);
        xh_wr32(xh_dbr(),(xh_slot&0xff)*4,DB1IN);      /* DB EP1-IN (DCI=3) */
        return -1;
    }
    xh_wr32(xh_dbr(),(xh_slot&0xff)*4,DB1IN);
    ev_t ev;
    for(unsigned i=0;i<256;i++){
        if(ev_peek(&ev)<=0) break;
        if(EV_TYPE(&ev)!=EVT_TRANSFER) continue;
        if(EV_PTR(&ev)!=ep1_sub_trb) continue;
        ep1_sub_idx=-1;
        uint8_t code=(uint8_t)EV_CODE(&ev);
        if(code!=1) return code==5 ? -2 : -3;
        long len=EV_LEN(&ev);
        if(len<0) len=0;
        if((uint32_t)len>max) len=max;
        memcpy(buf,ep1_buf,(size_t)len);
        return (int)len;
    }
    return -1;
}