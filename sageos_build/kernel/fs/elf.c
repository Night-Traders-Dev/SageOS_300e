#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "elf.h"

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} SageElf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} SageElf64_Phdr;

int elf_validate(const void *data) {
    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return 0;
    }
    return 1;
}

void elf_exec(const void *data) {
    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    if (!elf_validate(data)) {
        console_write("\nNot a valid ELF file.");
        return;
    }
    
    SageElf64_Phdr *phdr = (SageElf64_Phdr *)((uint8_t *)data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == 1) { // PT_LOAD
            uint8_t *dest = (uint8_t *)phdr[i].p_vaddr;
            uint8_t *src  = (uint8_t *)data + phdr[i].p_offset;
            for (uint64_t j = 0; j < phdr[i].p_filesz; j++) dest[j] = src[j];
            for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) dest[j] = 0;
        }
    }
    
    console_write("\nJumping to: ");
    console_hex64(ehdr->e_entry);
    ((void (*)(void))ehdr->e_entry)();
}
