// ThaisBoot - bootloader UEFI original (ThaÃ­s OS)
// Usa GNU-EFI apenas para o cabeÃ§alho PE/ABI correto; toda a logica de
// carregamento do kernel, paginacao e framebuffer eh nossa.
#include <efi.h>
#include <efilib.h>
#include "bootinfo.h"
#include "elf.h"

extern EFI_GUID GraphicsOutputProtocol;
extern EFI_GUID LoadedImageProtocol;
extern EFI_GUID FileSystemProtocol;

static void outb(uint16_t port, uint8_t v){ __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static void *memset(void *d, int c, uint64_t n){ uint8_t *p=d; while(n--) *p++=(uint8_t)c; return d; }

// debug via COM1 (capturado pelo -nographic do QEMU)
static void com_putc(char c){
    unsigned char st;
    do { __asm__ volatile("inb %1,%0" : "=a"(st) : "Nd"((unsigned short)0x3FD)); } while(!(st & 0x20));
    __asm__ volatile("outb %0,%1" :: "a"((unsigned char)c), "Nd"((unsigned short)0x3F8));
}
static void dbg_print(const char *s){ while(*s) com_putc(*s++); }
static void dbg_print64(uint64_t v){
    char b[20]; int i=0; b[i++]='0'; b[i++]='x';
    for(int s=60;s>=0;s-=4){ uint8_t d=(v>>s)&0xF; b[i++]= d<10?'0'+d:'a'+d-10; }
    b[i]=0; dbg_print(b);
}
static void panic(const char *msg){
    dbg_print("ThaisBoot PANIC: ");
    dbg_print(msg); dbg_print("\r\n");
    for(;;) __asm__ volatile("hlt");
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab){
    InitializeLib(image, systab);
    dbg_print("ThaisBoot iniciado\r\n");

    EFI_STATUS s;

    // ---------- abrir boot device (ESP) ----------
    EFI_LOADED_IMAGE *li = 0;
    s = BS->HandleProtocol(image, &LoadedImageProtocol, (void**)&li);
    if(EFI_ERROR(s)){ dbg_print("loaded image err "); dbg_print64(s); dbg_print("\r\n"); panic("loaded image"); }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    s = BS->HandleProtocol(li->DeviceHandle, &FileSystemProtocol, (void**)&fs);
    if(EFI_ERROR(s)){ dbg_print("fs proto err "); dbg_print64(s); dbg_print("\r\n"); panic("fs proto"); }

    EFI_FILE *root = 0;
    s = fs->OpenVolume(fs, &root);
    if(EFI_ERROR(s)) panic("open vol");

    EFI_FILE *kf = 0;
    s = root->Open(root, &kf, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if(EFI_ERROR(s)) panic("kernel.elf nao encontrado");

    // ler o arquivo (ate 16MB)
    UINTN bufsz = 16*1024*1024;
    VOID *buf = 0;
    s = BS->AllocatePool(EfiLoaderData, bufsz, &buf);
    if(EFI_ERROR(s)) panic("pool");
    UINTN rd = bufsz;
    s = kf->Read(kf, &rd, buf);
    if(EFI_ERROR(s)) panic("read kernel");
    uint64_t ksize = rd;
    if(ksize < 64) panic("kernel vazio");

    elf64_ehdr_t *eh = (elf64_ehdr_t*)buf;
    if(*(uint32_t*)eh->ident != ELF_MAGIC || eh->ident[4]!=ELF_CLASS_64) panic("elf invalido");
    if(eh->machine != EM_X86_64) panic("elf nao x86_64");
    dbg_print("ThaisBoot: kernel.elf ");
    dbg_print64(ksize);
    dbg_print(" bytes\r\n");

    // ---------- alocar regiao do kernel (2MB alinhado) ----------
    uint64_t kbase=0, ktop=0;
    for(int i=0;i<eh->phnum;i++){
        elf64_phdr_t *ph = (elf64_phdr_t*)(buf + eh->phoff + i*eh->phentsize);
        if(ph->type != PT_LOAD) continue;
        if(kbase==0 || ph->vaddr < kbase) kbase = ph->vaddr;
        uint64_t top = ph->vaddr + ph->memsz;
        if(top > ktop) ktop = top;
    }
    uint64_t img_size = ktop - kbase;
    uint64_t img_2mb = (img_size + 0x1FFFFF) >> 21;
    EFI_PHYSICAL_ADDRESS kphys=0;
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderCode, (img_2mb+1)*512, &kphys);
    if(EFI_ERROR(s)) panic("alloc kernel");
    kphys = (kphys + 0x1FFFFF) & ~0x1FFFFFULL;
    dbg_print("ThaisBoot: kernel phys ");
    dbg_print64(kphys);
    dbg_print("\r\n");
    for(int i=0;i<eh->phnum;i++){
        elf64_phdr_t *ph = (elf64_phdr_t*)(buf + eh->phoff + i*eh->phentsize);
        if(ph->type != PT_LOAD) continue;
        uint8_t *dst = (uint8_t*)(uint64_t)(kphys + (ph->vaddr - kbase));
        memset(dst, 0, ph->memsz);
        for(uint64_t j=0;j<ph->filesz;j++) dst[j] = ((uint8_t*)buf)[ph->offset + j];
    }

    // ---------- paginas: identidade 0..4GB + higher-half kernel (4 niveis) ----------
    EFI_PHYSICAL_ADDRESS pml4_p=0, pdpt_id_p=0, pd_id0_p=0, pd_id1_p=0, pd_id2_p=0, pd_id3_p=0, pdpt_hh_p=0, pd_hh_p=0; // FIX 4PDs
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pml4_p);   if(EFI_ERROR(s)) panic("pml4");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pdpt_id_p); if(EFI_ERROR(s)) panic("pdpt_id");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_id0_p);  if(EFI_ERROR(s)) panic("pd_id0");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_id1_p);  if(EFI_ERROR(s)) panic("pd_id1");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_id2_p);  if(EFI_ERROR(s)) panic("pd_id2");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_id3_p);  if(EFI_ERROR(s)) panic("pd_id3");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pdpt_hh_p); if(EFI_ERROR(s)) panic("pdpt_hh");
    s = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_hh_p);   if(EFI_ERROR(s)) panic("pd_hh");
    uint64_t *pml4    = (uint64_t*)(uint64_t)pml4_p;
    uint64_t *pdpt_id = (uint64_t*)(uint64_t)pdpt_id_p;
    uint64_t *pd_id0  = (uint64_t*)(uint64_t)pd_id0_p;
    uint64_t *pd_id1  = (uint64_t*)(uint64_t)pd_id1_p;
    uint64_t *pd_id2  = (uint64_t*)(uint64_t)pd_id2_p;
    uint64_t *pd_id3  = (uint64_t*)(uint64_t)pd_id3_p;
    uint64_t *pdpt_hh = (uint64_t*)(uint64_t)pdpt_hh_p;
    uint64_t *pd_hh   = (uint64_t*)(uint64_t)pd_hh_p;
    memset(pml4,0,4096); memset(pdpt_id,0,4096);
    memset(pd_id0,0,4096); memset(pd_id1,0,4096); memset(pd_id2,0,4096); memset(pd_id3,0,4096);
    memset(pdpt_hh,0,4096); memset(pd_hh,0,4096);
    for(uint64_t i=0;i<512;i++) pd_id0[i] = (i<<21) | 0x83;
    for(uint64_t i=0;i<512;i++) pd_id1[i] = ((512+i)<<21) | 0x83;
    for(uint64_t i=0;i<512;i++) pd_id2[i] = ((1024+i)<<21) | 0x83;
    for(uint64_t i=0;i<512;i++) pd_id3[i] = ((1536+i)<<21) | 0x83;
    for(uint64_t i=0;i<img_2mb+1 && i<512;i++) pd_hh[i] = ((uint64_t)kphys + i*0x200000) | 0x83;
    pdpt_id[0] = pd_id0_p | 0x03;
    pdpt_id[1] = pd_id1_p | 0x03;
    pdpt_id[2] = pd_id2_p | 0x03;
    pdpt_id[3] = pd_id3_p | 0x03;
    pdpt_hh[0x1FE] = pd_hh_p | 0x03;
    pml4[0]     = pdpt_id_p | 0x03;
    pml4[0x1FF] = pdpt_hh_p | 0x03;

    // ---------- framebuffer (GOP) ----------
    thais_boot_info_t bi; memset(&bi,0,sizeof(bi));
    bi.magic = THAIS_BOOT_MAGIC;
    bi.hhdm_offset = 0xffffffff80000000ULL;
    bi.kernel_phys = kphys;
    bi.kernel_size = (img_size + 0xFFF) & ~0xFFFULL;
    bi.kernel_virt = kbase;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=0;
    s = BS->LocateProtocol(&GraphicsOutputProtocol, 0, (void**)&gop);
    if(!EFI_ERROR(s) && gop){
        bi.fb_addr = (void*)(uint64_t)gop->Mode->FrameBufferBase;
        bi.fb_width = gop->Mode->Info->HorizontalResolution;
        bi.fb_height = gop->Mode->Info->VerticalResolution;
        bi.fb_bpp = 32;
        bi.fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
        dbg_print("ThaisBoot: GOP ");
        dbg_print64(bi.fb_width); dbg_print("x"); dbg_print64(bi.fb_height);
        dbg_print("\r\n");
    } else {
        dbg_print("ThaisBoot: sem GOP\r\n");
    }

    // ---------- memory map ----------
    UINTN msize=0, mapkey2=0, dsize=0; UINT32 dver=0;
    s = BS->GetMemoryMap(&msize, 0, &mapkey2, &dsize, &dver);
    VOID *mm=0;
    s = BS->AllocatePool(EfiLoaderData, msize+512, &mm);
    if(!EFI_ERROR(s)){
        msize += 512;
        BS->GetMemoryMap(&msize, (EFI_MEMORY_DESCRIPTOR*)mm, &mapkey2, &dsize, &dver);
        uint64_t count = msize/dsize;
        uint64_t mc=0;
        for(uint64_t i=0;i<count && mc<256;i++){
            EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)mm + i*dsize);
            int t = (d->Type==EfiConventionalMemory)?1:2;
            bi.memmap[mc].base = d->PhysicalStart;
            bi.memmap[mc].length = d->NumberOfPages<<12;
            bi.memmap[mc].type = t;
            mc++;
        }
        bi.memmap_count = mc;
    }

    // ---------- exit boot services ----------
    // IMPORTANTE: ler entry e bi ANTES do ExitBootServices, pois buf eh
    // EfiLoaderData (memoria de boot services, liberada pelo EBS).
    uint64_t entry = (uint64_t)eh->entry;
    uint64_t bi_p = (uint64_t)&bi;

    s = BS->ExitBootServices(image, mapkey2);
    if(EFI_ERROR(s)) panic("exitbs");

    dbg_print("ThaisBoot: pulando para kernel\r\n");
    dbg_print("entry="); dbg_print64(entry); dbg_print("\r\n");
    dbg_print("post pml4_1FF="); dbg_print64(pml4[0x1FF]); dbg_print("\r\n");
    dbg_print("post pdhh0="); dbg_print64(pd_hh[0]); dbg_print("\r\n");
    dbg_print("post code0="); dbg_print64(*(volatile uint64_t*)(uint64_t)kphys); dbg_print("\r\n");
    dbg_print("loader@"); dbg_print64((uint64_t)efi_main); dbg_print("\r\n");
    dbg_print("startbytes="); dbg_print64(*(volatile uint64_t*)(uint64_t)(kphys+0x3a70)); dbg_print("\r\n");

    // Reconfigurar UART 16550 (COM1) antes do jump ao kernel - via IO ports
    {
        outb(0x3F8+1, 0x00); outb(0x3F8+3, 0x80); outb(0x3F8+0, 0x01); outb(0x3F8+1, 0x00);
        outb(0x3F8+3, 0x03); outb(0x3F8+2, 0xC7); outb(0x3F8+4, 0x0B);
    }

    // salta para o kernel (sem retorno), habilitando PSE (paginas 2MB)
    __asm__ volatile(
        "mov %%cr4, %%rax\n\t"
        "or $0x10, %%rax\n\t"     // CR4.PSE = 1 (2MB pages)
        "mov %%rax, %%cr4\n\t"
        "mov %0, %%cr3\n\t"
        "mov %1, %%rdi\n\t"
        "jmp *%2"
        :
        : "r"(pml4_p), "r"(bi_p), "r"(entry)
        : "memory","rdi","rax");
    dbg_print("post-cr3\r\n");
    for(;;) __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
