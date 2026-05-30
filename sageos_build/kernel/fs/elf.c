/*
 * elf.c — ELF64 loader for SageOS
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "console.h"
#include "elf.h"
#include "sage_alloc.h"

/* -----------------------------------------------------------------------
 * Validation
 * ----------------------------------------------------------------------- */

int elf_validate(const void *data, uint64_t size) {
    if (!data || size < sizeof(SageElf64_Ehdr)) return 0;
    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) return 0;
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) return 0;
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) return 0;
    if (ehdr->e_type != ET_EXEC) return 0;

#if defined(__x86_64__)
    if (ehdr->e_machine != EM_X86_64) return 0;
#elif defined(__aarch64__)
    if (ehdr->e_machine != EM_AARCH64) return 0;
#elif defined(__riscv)
    if (ehdr->e_machine != EM_RISCV) return 0;
#endif

    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) return 0;
    return 1;
}

/* -----------------------------------------------------------------------
 * Execution
 * ----------------------------------------------------------------------- */

int elf_exec(const void *data, uint64_t size, char *const argv[], char *const envp[]) {
    if (!elf_validate(data, size)) return -1;

    const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)data;
    const SageElf64_Phdr *phdr = (const SageElf64_Phdr *)((const uint8_t *)data + ehdr->e_phoff);

    /* 1. Map segments */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_vaddr < ELF_VADDR_MIN || phdr[i].p_vaddr + phdr[i].p_memsz > ELF_VADDR_MAX) return -1;
        uint8_t *dest = (uint8_t *)phdr[i].p_vaddr;
        const uint8_t *src = (const uint8_t *)data + phdr[i].p_offset;
        for (uint64_t j = 0; j < phdr[i].p_filesz; j++) dest[j] = src[j];
        for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) dest[j] = 0;
    }

    /* 2. Prepare arguments (transient kernel memory) */
    int argc = 0; if (argv) while (argv[argc]) argc++;
    int envc = 0; if (envp) while (envp[envc]) envc++;

    uintptr_t *argv_ptrs = (uintptr_t *)sage_malloc((argc + 1) * sizeof(uintptr_t));
    uintptr_t *envp_ptrs = (uintptr_t *)sage_malloc((envc + 1) * sizeof(uintptr_t));

    /* 3. Allocate process stack (AFTER transient buffers to avoid collision) */
    uint8_t *stack_mem = (uint8_t *)sage_malloc(65536);
    if (!stack_mem) return -1;
    uint8_t *sp = stack_mem + 65536;

    /* 4. Copy strings and pointers to process stack */
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        sp -= len;
        for (size_t j = 0; j < len; j++) sp[j] = envp[i][j];
        envp_ptrs[i] = (uintptr_t)sp;
    }
    envp_ptrs[envc] = 0;

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        for (size_t j = 0; j < len; j++) sp[j] = argv[i][j];
        argv_ptrs[i] = (uintptr_t)sp;
    }
    argv_ptrs[argc] = 0;

    sp = (uint8_t *)((uintptr_t)sp & ~15); /* Align */

    /* Ensure final sp is aligned to 16-byte boundary */
    size_t total_ptr_size = 8 + (argc + 1) * 8 + (envc + 1) * 8;
    if (total_ptr_size % 16 != 0) {
        sp -= 8;
    }

    sp -= (envc + 1) * 8;
    for (int i = 0; i <= envc; i++) ((uint64_t *)sp)[i] = (uint64_t)envp_ptrs[i];
    
    sp -= (argc + 1) * 8;
    for (int i = 0; i <= argc; i++) ((uint64_t *)sp)[i] = (uint64_t)argv_ptrs[i];

    sp -= 8;
    *(uint64_t *)sp = (uint64_t)argc;


    sage_free(envp_ptrs);
    sage_free(argv_ptrs);

    /* 5. Jump to entry */
    uintptr_t entry = ehdr->e_entry;

#if defined(__x86_64__)
    __asm__ volatile ("mov %0, %%rsp\njmp *%1" : : "r"(sp), "r"(entry) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile ("mov sp, %0\nbr %1" : : "r"(sp), "r"(entry) : "memory");
#elif defined(__riscv)
    __asm__ volatile ("mv sp, %0\njr %1" : : "r"(sp), "r"(entry) : "memory");
#endif

    return 0;

}

#include "vfs.h"
#include "process.h"

long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    VfsStat st;
    if (vfs_stat(path, &st) < 0) return -VFS_ENOENT;
    void *buffer = sage_malloc(st.size);
    if (!buffer) return -VFS_ENOSPC;
    if (vfs_read(path, 0, buffer, st.size) != (int)st.size) { sage_free(buffer); return -VFS_EIO; }

    task_t *t = current_task();
    
    /* If we have a parent (we are a vfork child), save the parent's memory */
    if (t && t->parent && t->parent->elf_size > 0 && !t->parent->saved_elf_data) {
        t->parent->saved_elf_data = sage_malloc(t->parent->elf_size);
        if (t->parent->saved_elf_data) {
            memcpy(t->parent->saved_elf_data, (void*)t->parent->elf_base, t->parent->elf_size);
        }
    }

    /* Record our new ELF span */
    if (t) {
        const SageElf64_Ehdr *ehdr = (const SageElf64_Ehdr *)buffer;
        const SageElf64_Phdr *phdr = (const SageElf64_Phdr *)((const uint8_t *)buffer + ehdr->e_phoff);
        uintptr_t min_vaddr = -1ULL;
        uintptr_t max_vaddr = 0;
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD) {
                if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
                if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr) max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
            }
        }
        t->elf_base = min_vaddr;
        t->elf_size = max_vaddr - min_vaddr;
        t->heap_base = (max_vaddr + 4095) & ~4095;
        t->heap_end = t->heap_base;
        t->heap_limit = t->heap_base + 64 * 1024 * 1024;
    }


    int ret = elf_exec(buffer, st.size, argv, envp);
    sage_free(buffer);
    return (long)ret;
}
