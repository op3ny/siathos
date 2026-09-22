#ifndef THAISBOOT_BOOTINFO_H
#define THAISBOOT_BOOTINFO_H
#include <stdint.h>

#define THAIS_BOOT_MAGIC 0x54484149534F53ULL

typedef struct {
    uint64_t base;
    uint64_t length;
    uint64_t type;
} thais_memmap_entry_t;

typedef struct {
    uint64_t magic;
    void    *fb_addr;
    uint64_t fb_width;
    uint64_t fb_height;
    uint64_t fb_pitch;
    uint64_t fb_bpp;
    uint64_t memmap_count;
    thais_memmap_entry_t memmap[256];
    uint64_t rsdp;
    uint64_t rsdp2;
    uint64_t hhdm_offset;
    uint64_t kernel_phys;
    uint64_t kernel_size;
    uint64_t kernel_virt;
} thais_boot_info_t;

#endif
