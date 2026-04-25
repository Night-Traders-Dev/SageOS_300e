/*
 * elf.c — ELF64 loader for SageOS
 *
 * Validates ELF64 x86_64 executables, maps PT_LOAD segments into memory,
 * zeroes BSS, and jumps to the entry point.  If the ELF entry returns,
 * control flows back to the shell.
 */

#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "elf.h"

/* -----------------------------------------------------------------------
 * Validation
 * ----------------------------------------------------------------------- */

int elf_validate(const void *data, uint64_t size) {
    if (!data || size < sizeof(SageElf64_Ehdr)) {
        return 0;
    }

    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;

    /* Magic number */
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return 0;
    }

    /* Must be 64-bit */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return 0;
    }

    /* Must be little-endian */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return 0;
    }

    /* Must be executable */
    if (ehdr->e_type != ET_EXEC) {
        return 0;
    }

    /* Must target x86_64 */
    if (ehdr->e_machine != EM_X86_64) {
        return 0;
    }

    /* Program header table must fit within the file */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        return 0;
    }

    uint64_t ph_end = ehdr->e_phoff +
                      (uint64_t)ehdr->e_phentsize * ehdr->e_phnum;
    if (ph_end > size) {
        return 0;
    }

    return 1;
}

/* -----------------------------------------------------------------------
 * Execution
 * ----------------------------------------------------------------------- */

int elf_exec(const void *data, uint64_t size) {
    if (!elf_validate(data, size)) {
        console_write("\nelf: not a valid ELF64 x86_64 executable.");
        return -1;
    }

    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    const SageElf64_Phdr *phdr =
        (const SageElf64_Phdr *)((const uint8_t *)data + ehdr->e_phoff);

    console_write("\nelf: loading ");
    console_u32((uint32_t)ehdr->e_phnum);
    console_write(" program header(s)");

    int loaded = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }

        /* Bounds-check: segment data must fit in file */
        if (phdr[i].p_offset + phdr[i].p_filesz > size) {
            console_write("\nelf: segment ");
            console_u32((uint32_t)i);
            console_write(" extends past end of file.");
            return -1;
        }

        /* Bounds-check: virtual address range */
        if (phdr[i].p_vaddr < ELF_VADDR_MIN ||
            phdr[i].p_vaddr + phdr[i].p_memsz > ELF_VADDR_MAX) {
            console_write("\nelf: segment ");
            console_u32((uint32_t)i);
            console_write(" vaddr out of allowed range [0x400000, 0x10000000).");
            console_write("\n  vaddr: ");
            console_hex64(phdr[i].p_vaddr);
            console_write("  memsz: ");
            console_hex64(phdr[i].p_memsz);
            return -1;
        }

        /* Copy file data */
        uint8_t *dest = (uint8_t *)phdr[i].p_vaddr;
        const uint8_t *src = (const uint8_t *)data + phdr[i].p_offset;

        for (uint64_t j = 0; j < phdr[i].p_filesz; j++) {
            dest[j] = src[j];
        }

        /* Zero BSS (memsz > filesz) */
        for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
            dest[j] = 0;
        }

        console_write("\n  LOAD ");
        console_hex64(phdr[i].p_vaddr);
        console_write(" filesz=");
        console_hex64(phdr[i].p_filesz);
        console_write(" memsz=");
        console_hex64(phdr[i].p_memsz);

        loaded++;
    }

    if (loaded == 0) {
        console_write("\nelf: no PT_LOAD segments found.");
        return -1;
    }

    console_write("\nelf: jumping to entry ");
    console_hex64(ehdr->e_entry);

    /* Entry-point bounds check */
    if (ehdr->e_entry < ELF_VADDR_MIN || ehdr->e_entry >= ELF_VADDR_MAX) {
        console_write("\nelf: entry point outside allowed range.");
        return -1;
    }

    /* Call the entry point.  If it returns, we get back to the shell. */
    int (*entry)(void) = (int (*)(void))ehdr->e_entry;
    int ret = entry();

    console_write("\nelf: returned with code ");
    if (ret < 0) {
        console_write("-");
        console_u32((uint32_t)(-ret));
    } else {
        console_u32((uint32_t)ret);
    }

    return ret;
}
