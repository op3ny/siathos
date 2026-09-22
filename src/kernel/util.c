#include "thais.h"
#include <stdarg.h>
#include <stdbool.h>

void *memset(void *s,int c,size_t n){ uint8_t *p=s; while(n--) *p++=(uint8_t)c; return s; }
void *memcpy(void *d,const void *s,size_t n){ uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; return d; }
void *memmove(void *d,const void *s,size_t n){ uint8_t *a=d; const uint8_t *b=s; if(a<b){ while(n--) *a++=*b++; } else { a+=n; b+=n; while(n--) *--a=*--b; } return d; }
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char *a,const char *b){ while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char *b,size_t n){ size_t i=0; while(i<n && *a && *a==*b){a++;b++;i++;} if(i==n) return 0; return (unsigned char)*a-(unsigned char)*b; }
char *strcpy(char *d,const char *s){ char *r=d; while((*d=*s)){d++;s++;} return r; }
char *strncpy(char *d,const char *s,size_t n){ char *r=d; size_t i=0; while(i<n && *s){ *d++=*s++; i++; } while(i<n){ *d++=0; i++; } return r; }
char *strchr(const char *s,int c){ while(*s){ if(*s==(char)c) return (char*)s; s++; } return 0; }
char *strrchr(const char *s,int c){ const char *last=0; while(*s){ if(*s==(char)c) last=s; s++; } if(c==0) return (char*)s; return (char*)last; }
static uint64_t rng_state=0x74686169736F73ULL;
uint64_t rand64(void){ rng_state ^= rng_state<<13; rng_state ^= rng_state>>7; rng_state ^= rng_state<<17; return rng_state; }
void slow_down(void){ __asm__ volatile("jmp 1f; 1: jmp 2f; 2:"::); }
void outb_port(uint16_t port,uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
void outw_port(uint16_t port,uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port)); }
uint8_t inb_port(uint16_t port){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }
uint16_t inw_port(uint16_t port){ uint16_t r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(port)); return r; }
uint32_t inl_port(uint16_t port){ uint32_t r; __asm__ volatile("inl %1,%0":"=a"(r):"Nd"(port)); return r; }
void outl_port(uint16_t port,uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port)); }
void halt(void){ for(;;) __asm__ volatile("cli; hlt"); }
uint64_t rdtsc64(void){ uint32_t lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((uint64_t)hi<<32)|lo; }
void delay_busy(uint64_t iters){ for(volatile uint64_t i=0;i<iters;i++) __asm__ volatile("pause"); }
void yield_to_scheduler(void){ sched_yield(); }
static inline void outb_io(uint16_t p,uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw_io(uint16_t p,uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
void reboot(void){ kprint("reboot: reiniciando...\n"); for(volatile int i=0;i<500000;i++) __asm__ volatile("pause"); __asm__ volatile("cli"); outb_io(0x64,0xFE); for(;;) __asm__ volatile("hlt"); }
void poweroff(void){ kprint("poweroff: desligando...\n"); for(volatile int i=0;i<500000;i++) __asm__ volatile("pause"); outw_io(0x604,0x2000); outw_io(0xB004,0x2000); __asm__ volatile("cli"); for(;;) __asm__ volatile("hlt"); }

int snprintf(char *buf,size_t n,const char *fmt,...){
    va_list ap; va_start(ap,fmt);
    size_t pos=0;
    for(const char *p=fmt;*p && pos+1<n;p++){
        if(*p=='%' && *(p+1)){
            p++;
            int width=0; bool zero_pad=false;
            if(*p=='0'){ zero_pad=true; p++; }
            while(*p>='0' && *p<='9'){ width=width*10+(*p-'0'); p++; }
            if(*p=='s'){ const char *s=va_arg(ap,const char*); if(!s) s="(null)"; while(*s && pos+1<n) buf[pos++]=*s++; }
            else if(*p=='c'){ char c=(char)va_arg(ap,int); if(pos+1<n) buf[pos++]=c; }
            else if(*p=='d' || *p=='u'){
                int v=va_arg(ap,int); char tmp[32]; int len=0; bool neg=false; if(v<0 && *p=='d'){neg=true; v=-v;} do{tmp[len++]='0'+(v%10); v/=10;}while(v); if(neg && pos+1<n) buf[pos++]='-';
                int pad = width - len - (neg?1:0); if(pad>0 && zero_pad){ while(pad-- && pos+1<n) buf[pos++]='0'; }
                else if(pad>0){ while(pad-- && pos+1<n) buf[pos++]=' '; }
                for(int i=len-1;i>=0 && pos+1<n;i--) buf[pos++]=tmp[i];
            }
            else if(*p=='x' || *p=='X'){
                unsigned v=va_arg(ap,unsigned); char tmp[32]; int len=0; do{int d=v%16; tmp[len++]= d<10?'0'+d:'a'+d-10; v/=16;}while(v); if(len==0) tmp[len++]='0';
                int pad = width - len; if(pad>0){ char pc = zero_pad?'0':' '; while(pad-- && pos+1<n) buf[pos++] = pc; }
                for(int i=len-1;i>=0 && pos+1<n;i--) buf[pos++]=tmp[i];
            }
            else if(*p=='l' && *(p+1)=='l'){
                p+=2;
                if(*p=='x' || *p=='X'){
                    unsigned long long v=va_arg(ap,unsigned long long); char tmp[32]; int len=0; do{int d=v%16; tmp[len++]= d<10?'0'+d:'a'+d-10; v/=16;}while(v); if(len==0) tmp[len++]='0';
                    int pad = width - len; if(pad>0){ char pc = zero_pad?'0':' '; while(pad-- && pos+1<n) buf[pos++] = pc; }
                    for(int i=len-1;i>=0 && pos+1<n;i--) buf[pos++]=tmp[i];
                } else if(*p=='u' || *p=='d'){
                    unsigned long long v=va_arg(ap,unsigned long long); char tmp[32]; int len=0; bool neg=false; if((long long)v<0 && *p=='d'){neg=true; v=-(long long)v;} do{tmp[len++]='0'+(v%10); v/=10;}while(v); if(neg && pos+1<n) buf[pos++]='-';
                    int pad = width - len - (neg?1:0); if(pad>0 && zero_pad){ while(pad-- && pos+1<n) buf[pos++]='0'; } else if(pad>0){ while(pad-- && pos+1<n) buf[pos++]=' '; }
                    for(int i=len-1;i>=0 && pos+1<n;i--) buf[pos++]=tmp[i];
                } else { if(pos+1<n) buf[pos++]=*p; }
            }
            else { if(pos+1<n) buf[pos++]=*p; }
        } else { buf[pos++]=*p; }
    }
    buf[pos]=0; va_end(ap); return (int)pos;
}

// PMM REAL
#define PMM_BITMAP_SIZE (64*1024)
static uint64_t pmm_bitmap[PMM_BITMAP_SIZE/8];
static uint64_t pmm_pages_total=0;
static uint64_t pmm_pages_used=0;
static uint64_t pmm_base=0;

void pmm_reserve(uint64_t base, uint64_t size){
    if(size==0) return;
    uint64_t start = base & ~0xFFFULL;
    uint64_t end = (base + size + 0xFFFULL) & ~0xFFFULL;
    for(uint64_t a=start;a<end;a+=0x1000){
        uint64_t idx=a>>12; if(idx>=pmm_pages_total) continue;
        if(!(pmm_bitmap[idx>>6] & (1ULL<<(idx&63)))){ pmm_bitmap[idx>>6] |= (1ULL<<(idx&63)); pmm_pages_used++; }
    }
}

void pmm_init(thais_boot_info_t *bi){
    if(!bi || bi->magic != THAIS_BOOT_MAGIC){ kprint("[arkhe] ERRO: boot info invalido\n"); halt(); }
    pmm_pages_total=0;
    uint64_t top=0;
    for(uint64_t i=0;i<bi->memmap_count;i++){
        uint64_t end=bi->memmap[i].base + bi->memmap[i].length;
        if(end>top) top=end;
    }
    if(top==0) top=0x40000000;
    pmm_base=0;
    pmm_pages_total=(top>>12)+1;
    if(pmm_pages_total>PMM_BITMAP_SIZE*8){ kprint("[arkhe] PMM: memoria acima de 2GB ignorada (bitmap fixo)\n"); pmm_pages_total=PMM_BITMAP_SIZE*8; }
    memset(pmm_bitmap,0xFF,sizeof(pmm_bitmap));
    for(uint64_t i=0;i<bi->memmap_count;i++){
        if(bi->memmap[i].type!=1) continue;
        uint64_t start=bi->memmap[i].base; uint64_t len=bi->memmap[i].length;
        uint64_t pg_start=(start+0xFFF)&~(uint64_t)0xFFF;
        uint64_t pg_count=len>>12;
        uint64_t first=pg_start>>12;
        for(uint64_t p=0;p<pg_count && (first+p)<pmm_pages_total;p++){
            uint64_t idx=first+p;
            pmm_bitmap[idx>>6] &= ~(1ULL<<(idx&63));
        }
    }
    // reserva kernel, framebuffer e estruturas do bootloader
    if(bi->kernel_phys && bi->kernel_size) pmm_reserve(bi->kernel_phys, bi->kernel_size);
    if(bi->fb_addr && bi->fb_width && bi->fb_height) pmm_reserve((uint64_t)bi->fb_addr, bi->fb_width*bi->fb_height*(bi->fb_bpp/8));
    // reserva a propria estrutura boot_info (esta em memoria identidade)
    pmm_reserve((uint64_t)bi, sizeof(thais_boot_info_t));

    // RESERVA "Low Memory" legado (0x000-0x9FFFF): IVT, BDA, EBDA, BIOS/codigo
    // real-mode e dados do bootloader. O carve-out acima so libera regioes do
    // memory map marcado como "disponivel"; a memoria abaixo da 1a regiao
    // disponivel e firmware-documentada e NAO pode ser entregue ao pmm_alloc
    // (causava pmm_alloc devolvendo a pagina 0x1000 como PML4/stack livre -> 
    // crash com salto para lixo 0xa0a12000a0a12).
    uint64_t low_used=0;
    for(uint64_t i=0;i<bi->memmap_count;i++){
        if(bi->memmap[i].type==1){ low_used = bi->memmap[i].base; break; }
    }
    if(low_used==0 || low_used > 0x100000) low_used = 0x100000;   /* 1MB: seguranca */
    pmm_reserve(0, low_used);

    // recalcula used a partir do bitmap
    pmm_pages_used=0;
    for(uint64_t i=0;i<pmm_pages_total;i++) if(pmm_bitmap[i>>6] & (1ULL<<(i&63))) pmm_pages_used++;
    kprint("[arkhe] PMM: ");
    char b[48]; snprintf(b,48,"%llu paginas (%llu MB) gerenciadas\n", (unsigned long long)pmm_pages_total, (unsigned long long)(pmm_pages_total<<12>>20));
    kprint(b);
}

void *pmm_alloc(size_t pages){
    if(pages==0) return 0;
    for(uint64_t i=0;i<pmm_pages_total;i++){
        bool free=true;
        for(uint64_t b=0;b<pages;b++){
            uint64_t idx=i+b;
            if(idx>=pmm_pages_total || (pmm_bitmap[idx>>6] & (1ULL<<(idx&63)))){ free=false; break; }
        }
        if(free){
            for(uint64_t b=0;b<pages;b++){ uint64_t idx=i+b; pmm_bitmap[idx>>6] |= (1ULL<<(idx&63)); }
            pmm_pages_used+=pages;
            return (void*)((i<<12)+pmm_base);
        }
    }
    kprint("[arkhe] PMM: sem memoria livre\n");
    return 0;
}
void pmm_free(void *addr, size_t pages){
    if(!addr || pages==0) return;
    uint64_t base=((uint64_t)addr - pmm_base)>>12;
    uint64_t freed=0;
    for(uint64_t b=0;b<pages;b++){
        uint64_t idx=base+b;
        if(idx>=pmm_pages_total) continue;
        if(pmm_bitmap[idx>>6] & (1ULL<<((idx)&63))){ freed++; }
        pmm_bitmap[idx>>6] &= ~(1ULL<<((idx)&63));
    }
    if(freed > pmm_pages_used) pmm_pages_used=0;
    else pmm_pages_used-=freed;
}
/* true se as 'pages' paginas a partir da fisica 'base' estao TODAS livres.
   Usado pelo ELF loader para garantir que um segmento so e carregado em
   regioes que o PMM nunca entregou a outro subsistema (evita corromper
   kernel/heap/paginas de outro processo). */
bool pmm_range_free(uint64_t base, size_t pages){
    uint64_t idx=base>>12;
    for(size_t b=0;b<pages;b++){
        uint64_t i=idx+b;
        if(i>=pmm_pages_total) return false;
        if(pmm_bitmap[i>>6] & (1ULL<<(i&63))) return false;
    }
    return true;
}
uint64_t pmm_total_mem(void){ return pmm_pages_total<<12; }
uint64_t pmm_free_mem(void){ return (pmm_pages_total-pmm_pages_used)<<12; }

#define HEAP_MAGIC 0x7A15
typedef struct heap_node {
    uint32_t magic;
    size_t size;
    bool free;
    struct heap_node *next;
} heap_node_t;
static heap_node_t *heap_head=0;
static bool heap_ready=false;

void heap_init(void){
    size_t blk=1024;
    void *mem=pmm_alloc(blk);
    if(!mem){ kprint("[oikos] heap: falha PMM\n"); return; }
    heap_head=(heap_node_t*)mem;
    heap_head->magic=HEAP_MAGIC;
    heap_head->size=(blk<<12)-sizeof(heap_node_t);
    heap_head->free=true;
    heap_head->next=0;
    heap_ready=true;
    kprint("[oikos] heap: 4MB prontos\n");
}
void *kmalloc(size_t size){
    if(!heap_ready || size==0) return 0;
    size=(size+15)&~(size_t)15;
    heap_node_t *cur=heap_head;
    while(cur){
        if(cur->magic!=HEAP_MAGIC){ kprint("[oikos] heap CORROMPIDO\n"); halt(); }
        if(cur->free && cur->size>=size){
            if(cur->size >= size+sizeof(heap_node_t)+32){
                heap_node_t *split=(heap_node_t*)((uint8_t*)cur+sizeof(heap_node_t)+size);
                split->magic=HEAP_MAGIC;
                split->size=cur->size - size - sizeof(heap_node_t);
                split->free=true;
                split->next=cur->next;
                cur->size=size;
                cur->next=split;
            }
            cur->free=false;
            return (uint8_t*)cur+sizeof(heap_node_t);
        }
        cur=cur->next;
    }
    void *more=pmm_alloc(256);
    if(!more) return 0;
    heap_node_t *node=(heap_node_t*)more;
    node->magic=HEAP_MAGIC; node->size=(256<<12)-sizeof(heap_node_t); node->free=true; node->next=0;
    heap_node_t *last=heap_head; while(last->next) last=last->next; last->next=node;
    return kmalloc(size);
}
void heap_walk_quick(void){
    heap_node_t *q=heap_head;
    for(int i=0;i<3 && q;i++){
        char qb[140]; snprintf(qb,140,"[oikos] q n=%d ptr=0x%llx size=%llu free=%d next=0x%llx magic=%u\n",
            i,(unsigned long long)(uintptr_t)q,(unsigned long long)q->size,q->free?1:0,
            (unsigned long long)(uintptr_t)q->next, q->magic); kprint(qb);
        q=q->next;
    }
}
void kfree(void *ptr){
    if(!ptr) return;
    heap_node_t *cur=heap_head;
    while(cur){ if((uint8_t*)cur+sizeof(heap_node_t)==ptr){ cur->free=true;
            heap_node_t *n=heap_head;
            while(n && n->next){ if(n->free && n->next->free){ n->size+=sizeof(heap_node_t)+n->next->size; n->next=n->next->next; } else n=n->next; }
            return; }
        cur=cur->next; }
}
