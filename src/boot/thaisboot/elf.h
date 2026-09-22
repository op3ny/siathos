#ifndef THAISBOOT_ELF_H
#define THAISBOOT_ELF_H
#include <stdint.h>

#define ELF_MAGIC 0x464C457F // 0x7F 'E' 'L' 'F'
typedef struct {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf64_phdr_t;

#define PT_LOAD 1
#define ELF_CLASS_64 2
#define ELF_DATA_2LSB 1
#define EM_X86_64 0x3E

#endif
