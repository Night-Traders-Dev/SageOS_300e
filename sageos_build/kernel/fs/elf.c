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
#include "sage_alloc.h"

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

    /* Must target current architecture */
#if defined(__x86_64__)
    if (ehdr->e_machine != EM_X86_64) {
        return 0;
    }
#elif defined(__aarch64__)
    if (ehdr->e_machine != EM_AARCH64) {
        return 0;
    }
#elif defined(__riscv)
    if (ehdr->e_machine != EM_RISCV) {
        return 0;
    }
#endif

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

int elf_exec(const void *data, uint64_t size, char *const argv[], char *const envp[]) {
    if (!elf_validate(data, size)) {
        console_write("\nelf: not a valid ELF64 executable.");
        return -1;
    }

    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    const SageElf64_Phdr *phdr =
        (const SageElf64_Phdr *)((const uint8_t *)data + ehdr->e_phoff);

    console_write("\nelf: loading segments...");
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        /* Copy file data and zero BSS */
        uint8_t *dest = (uint8_t *)phdr[i].p_vaddr;
        const uint8_t *src = (const uint8_t *)data + phdr[i].p_offset;
        for (uint64_t j = 0; j < phdr[i].p_filesz; j++) dest[j] = src[j];
        for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) dest[j] = 0;
    }

    /* Stack Setup */
    /* We allocate a simple 64KB stack for the process */
    uint8_t *stack_top = (uint8_t *)sage_malloc(65536);
    if (!stack_top) return -1;
    uint8_t *sp = stack_top + 65536;

    /* Push argv/envp onto stack (simplified for now) */
    /* Real layout: [argc][argv pointers...][NULL][envp pointers...][NULL][strings...] */
    
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }

    /* We'll just push argc and argv for now to satisfy crt0.S 
       Actually, our crt0 expects argc at (%rsp) and argv pointers following.
    */
    
    /* Align SP to 16 bytes */
    sp = (uint8_t *)((uintptr_t)(sp - 16) & ~15);

    /* Push arguments onto stack (this is a bit complex in C, 
       usually done by pushing strings first, then pointers) */
    
    /* For now, let's just push a dummy argc/argv to avoid crashes */
    sp -= 8; *(uint64_t *)sp = 0; /* NULL envp */
    sp -= 8; *(uint64_t *)sp = 0; /* NULL argv[0] */
    sp -= 8; *(uint64_t *)sp = (uint64_t)argc;

    /* Architecture specific jump to entry point with new SP */
    int ret = 0;
    uintptr_t entry = ehdr->e_entry;

#if defined(__x86_64__)
    __asm__ volatile (
        "mov %1, %%rsp\n"
        "call *%2\n"
        "mov %%eax, %0"
        : "=r"(ret)
        : "r"(sp), "r"(entry)
        : "rax", "memory"
    );
#elif defined(__aarch64__)
    __asm__ volatile (
        "mov sp, %1\n"
        "blr %2\n"
        "mov %0, x0"
        : "=r"(ret)
        : "r"(sp), "r"(entry)
        : "x0", "memory"
    );
#elif defined(__riscv)
    __asm__ volatile (
        "mv sp, %1\n"
        "jalr %2\n"
        "mv %0, a0"
        : "=r"(ret)
        : "r"(sp), "r"(entry)
        : "a0", "memory"
    );
#endif

    sage_free(stack_top);
    return ret;
}

#include "vfs.h"
#include "sage_alloc.h"

long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    (void)argv; (void)envp;

    VfsStat st;
    if (vfs_stat(path, &st) < 0) {
        return -VFS_ENOENT;
    }

    /* Allocate buffer for the entire ELF file */
    /* Using sage_malloc for now as a simple kernel-side allocator */
    void *buffer = sage_malloc(st.size);
    if (!buffer) {
        return -VFS_ENOSPC;
    }

    if (vfs_read(path, 0, buffer, st.size) != (int)st.size) {
        sage_free(buffer);
        return -VFS_EIO;
    }

    /* Execute the ELF */
    int ret = elf_exec(buffer, st.size, argv, envp);

    /* We don't free the buffer if it's still running, 
       but here elf_exec calls it and waits for return. */
    sage_free(buffer);

    return (long)ret;
}
