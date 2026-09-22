typedef unsigned long long u64;
typedef unsigned short u16;
typedef u64 efi_status_t;

extern void _reloc_anchor(void);
void * volatile g_anchor = (void*)_reloc_anchor;

static void outb(unsigned short port, unsigned char v){
    __asm__ volatile("out %0,%1" :: "a"(v), "Nd"(port));
}
static void com_puts(const char *s){
    while(*s){ outb(0x3F8, (unsigned char)*s); s++; }
}

efi_status_t __attribute__((ms_abi, used))
efi_main(void *image, void *systable){
    (void)g_anchor;
    com_puts("THAISTEST OK\r\n");
    for(;;) __asm__ volatile("hlt");
    return 0;
}
