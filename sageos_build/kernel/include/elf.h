#ifndef SAGEOS_ELF_H
#define SAGEOS_ELF_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * ELF64 constants
 * ----------------------------------------------------------------------- */

/* e_ident indices */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_NIDENT     16

/* e_ident[EI_MAGx] */
#define ELFMAG0       0x7F
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'

/* e_ident[EI_CLASS] */
#define ELFCLASS64    2

/* e_ident[EI_DATA] */
#define ELFDATA2LSB   1   /* Little-endian */

/* e_type */
#define ET_EXEC       2   /* Executable */

/* e_machine */
#define EM_X86_64     62

/* Program header p_type */
#define PT_NULL       0
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_NOTE       4
#define PT_PHDR       6

/* Program header p_flags */
#define PF_X          0x1  /* Execute */
#define PF_W          0x2  /* Write */
#define PF_R          0x4  /* Read */

/* Allowed virtual address range for loaded ELF segments */
#define ELF_VADDR_MIN 0x200000ULL
#define ELF_VADDR_MAX 0x10000000ULL

/* -----------------------------------------------------------------------
 * ELF64 header
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
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

/* -----------------------------------------------------------------------
 * ELF64 program header
 * ----------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/*
 * Validate an ELF64 x86_64 executable.
 *
 * Returns 1 if valid, 0 if not.
 * @data:  pointer to the raw ELF file in memory
 * @size:  byte length of the ELF file buffer
 */
int elf_validate(const void *data, uint64_t size);

/*
 * Load and execute an ELF64 executable.
 *
 * Maps PT_LOAD segments, zeroes BSS, jumps to e_entry.
 * If the ELF entry returns, the return value is propagated.
 *
 * Returns:  entry function return code, or -1 on load error.
 * @data:  pointer to the raw ELF file in memory
 * @size:  byte length of the ELF file buffer
 */
int elf_exec(const void *data, uint64_t size);

#endif /* SAGEOS_ELF_H */
