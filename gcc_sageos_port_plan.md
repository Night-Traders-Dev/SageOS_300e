# GCC Port Plan for SageOS

> **Purpose of this document:** This is a complete engineering plan for porting GCC to SageOS.
> It is intended as a persistent context document for an AI coding assistant (Gemini).
> All implementation requests should be understood against this plan.

---

## 1. Project Context

### 1.1 What SageOS Is

SageOS is a hybrid operating system hosted at `github.com/Night-Traders-Dev/SageOS`.

- **Kernel layer:** Written in C. Handles hardware init, bootstrapping, interrupt handling, MetalVM interpreter, and performance-critical shims.
- **Runtime layer:** Written in SageLang (a custom language). Handles system services, shell commands, VFS high-level logic, init/service orchestration.
- **MetalVM:** A bytecode interpreter embedded in the kernel. SageLang compiles to MetalVM bytecode and executes inside the OS at runtime.
- **Architectures:** x86_64, AArch64 (arm64), RISC-V 64-bit (rv64). Each is a submodule under `arch/`.
- **VFS:** FAT32, BTRFS, SWAP support implemented. VFS bridge between high-level SageLang VFS logic and the low-level C kernel.
- **Networking:** lwip and mbedtls are integrated as custom submodule forks.
- **Build targets:** QEMU virtual machines for all three arches; bare-metal Lenovo 300e (x64) and Raspberry Pi 4 (arm64).

### 1.2 Repository Layout

```
SageOS/
├── sageos_build/
│   ├── kernel/          ← C kernel: boot, HAL, VFS bridge, MetalVM host
│   ├── sage_lang/       ← SageLang compiler, runtime, stdlib
│   └── actual_sagelang_build/  ← host-side SageLang build utilities
├── arch/
│   ├── x64/             ← x86_64 port + QEMU + Lenovo 300e target
│   ├── arm64/           ← AArch64 port + QEMU + RPi4 target
│   └── rv64/            ← RISC-V 64 port + QEMU target
├── docs/
├── scripts/
├── sageos.sh            ← top-level build/run manager
└── setup_submodules.sh  ← submodule initializer (use instead of --recursive)
```

### 1.3 Current State Relevant to GCC

| Component | Status |
|---|---|
| VFS read/write (FAT32) | ✅ Working |
| Shell with file commands | ✅ Working |
| UART/console output | ✅ Working |
| Syscall table | ✅ Implemented (20+ calls) |
| File descriptor table | ✅ Implemented (32 per task) |
| Per-process heap (`brk`) | ✅ Implemented (64MB default) |
| Process model (`fork`/`exec`) | ✅ Implemented (vfork, waitpid, exit) |
| C runtime (`crt0`) | ✅ Implemented (Unified crt0.S) |
| Newlib or any libc | ✅ Stubs ready, Porting in progress |

---

## 2. Goal Definition

### 2.1 Phase Goals

| Phase | Goal | Depends On |
|---|---|---|
| 0 | Syscall entry + dispatch in kernel | Nothing |
| 1 | Cross-compiler: `x86_64-sageos` on Linux | Phase 0 not required for build, but needed to test output |
| 2 | Newlib port with SageOS syscall stubs | Phase 0 |
| 3 | Hello World binary runs on SageOS QEMU | Phases 0, 1, 2 |
| 4 | libgcc targeting SageOS | Phase 2 |
| 5 | Native GCC running on SageOS | Phases 0–4 + process model |

### 2.2 Out of Scope (for now)

- Dynamic linking / shared libraries
- Threads / pthreads
- Full POSIX process model (sigaction, ptrace, etc.)
- C++ standard library (libstdc++) — defer until after libgcc
- GCC plugins

---

## 3. Phase 0 — Syscall Infrastructure

This is the kernel-side foundation everything else depends on.

### 3.1 Syscall Numbers

Use Linux-compatible syscall numbers where possible (makes Newlib stubs simpler):

```c
/* sageos_build/kernel/include/syscall_numbers.h */

#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_fstat       5
#define SYS_lseek       8
#define SYS_brk        12
#define SYS_exit       60
#define SYS_getpid     39
#define SYS_isatty    100   /* custom, not Linux */
#define SYS_kill        62
#define SYS_times      100  /* repurpose or offset */
```

### 3.2 Syscall Entry Points (per arch)

**x64 — `arch/x64/kernel/syscall_entry.S`:**

```asm
.global syscall_entry
syscall_entry:
    ; On x64 with `syscall` instruction:
    ; rax = syscall number
    ; rdi, rsi, rdx, r10, r8, r9 = args
    ; Return value in rax
    push rbp
    mov rbp, rsp
    ; Save caller-save registers
    push rcx
    push r11
    ; Call C dispatcher
    mov rcx, r10          ; 4th arg: Linux ABI uses r10 instead of rcx
    call syscall_dispatch
    pop r11
    pop rcx
    pop rbp
    sysretq
```

**arm64 — `arch/arm64/kernel/syscall_entry.S`:**

```asm
.global el0_sync_handler
el0_sync_handler:
    ; x8 = syscall number (Linux arm64 ABI)
    ; x0–x5 = args
    ; Return in x0
    stp x29, x30, [sp, #-16]!
    mov x0, x8           ; syscall number as first C arg
    ; x1–x5 already in place as args 2–6
    bl syscall_dispatch
    ldp x29, x30, [sp], #16
    eret
```

**rv64 — `arch/rv64/kernel/syscall_entry.S`:**

```asm
.global handle_ecall
handle_ecall:
    ; a7 = syscall number (Linux rv64 ABI)
    ; a0–a5 = args
    ; Return in a0
    addi sp, sp, -16
    sd ra, 0(sp)
    mv a0, a7            ; syscall number as first C arg
    call syscall_dispatch
    ld ra, 0(sp)
    addi sp, sp, 16
    sret
```

### 3.3 Syscall Dispatcher — C Side

```c
/* sageos_build/kernel/syscall.c */

#include "syscall_numbers.h"
#include "vfs.h"
#include "process.h"
#include "mm.h"

long syscall_dispatch(long num, long a1, long a2, long a3,
                      long a4, long a5) {
    switch (num) {
    case SYS_write:
        return sys_write((int)a1, (const void *)a2, (size_t)a3);
    case SYS_read:
        return sys_read((int)a1, (void *)a2, (size_t)a3);
    case SYS_open:
        return sys_open((const char *)a1, (int)a2, (int)a3);
    case SYS_close:
        return sys_close((int)a1);
    case SYS_lseek:
        return sys_lseek((int)a1, (off_t)a2, (int)a3);
    case SYS_fstat:
        return sys_fstat((int)a1, (struct stat *)a2);
    case SYS_brk:
        return sys_brk(a1);
    case SYS_exit:
        sys_exit((int)a1);   /* noreturn */
    case SYS_getpid:
        return sys_getpid();
    case SYS_kill:
        return sys_kill((int)a1, (int)a2);
    default:
        return -ENOSYS;
    }
}
```

### 3.4 File Descriptor Table

Add to the task/process struct in the scheduler:

```c
/* sageos_build/kernel/include/process.h */

#define MAX_FD 32

typedef struct {
    int      valid;
    vfs_fd_t vfs_handle;
    int      flags;
    off_t    offset;
} fd_entry_t;

typedef struct task {
    /* ... existing fields ... */
    fd_entry_t fd_table[MAX_FD];
    uintptr_t  heap_end;      /* for sys_brk */
    uintptr_t  heap_base;
    uintptr_t  heap_limit;
} task_t;
```

Pre-populate fd 0, 1, 2 (stdin, stdout, stderr) pointing to the console/UART path on task creation.

### 3.5 `sys_brk` Implementation

```c
/* sageos_build/kernel/mm.c */

long sys_brk(uintptr_t addr) {
    task_t *t = current_task();
    if (addr == 0)
        return (long)t->heap_end;
    if (addr < t->heap_base || addr > t->heap_limit)
        return -ENOMEM;
    t->heap_end = addr;
    return (long)addr;
}
```

Each task needs a dedicated heap region. On QEMU, a fixed 4MB region per task is sufficient initially. Allocate from a kernel-managed pool at task creation.

---

## 4. Phase 1 — GCC Configuration for SageOS Target

### 4.1 Target Triple

The GCC target triple format: `<cpu>-<vendor>-<os>`.

- x64:   `x86_64-unknown-sageos`
- arm64: `aarch64-unknown-sageos`
- rv64:  `riscv64-unknown-sageos`

### 4.2 Generic OS Config Header

Create `gcc/config/sageos.h` in the GCC source tree:

```c
/* gcc/config/sageos.h — SageOS OS-specific GCC configuration */

#ifndef _SAGEOS_H
#define _SAGEOS_H

/* Identify the OS in preprocessor */
#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()            \
  do {                                      \
    builtin_define ("__sageos__");          \
    builtin_define ("__unix__");            \
    builtin_assert ("system=sageos");       \
  } while (0)

/* Link against libc; no dynamic linking */
#undef  LIB_SPEC
#define LIB_SPEC "-lc"

/* Startup file */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC ""

/* No dynamic linker */
#undef  LINK_SPEC
#define LINK_SPEC "-static"

/* No shared libraries */
#undef  SUPPORTS_SHARED
#define SUPPORTS_SHARED 0

/* Size of types — keep consistent with newlib */
#undef  SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#endif /* _SAGEOS_H */
```

### 4.3 Arch-Specific Overrides

**`gcc/config/i386/sageos.h`:**

```c
#include "sageos.h"

/* x86_64 SageOS uses System V AMD64 ABI */
#undef  ASM_SPEC
#define ASM_SPEC "--64"

#undef  MULTILIB_DEFAULTS
```

**`gcc/config/aarch64/sageos.h`:**

```c
#include "sageos.h"
/* AArch64 — nothing extra needed beyond generic */
```

**`gcc/config/riscv/sageos.h`:**

```c
#include "sageos.h"
/* RV64GC baseline */
#undef  RISCV_TUNE_STRING_DEFAULT
#define RISCV_TUNE_STRING_DEFAULT "generic"
```

### 4.4 Wire into `gcc/config.gcc`

Find the `case ${target} in` block and add:

```
*-*-sageos*)
  tm_file="${tm_file} sageos.h"
  # arch-specific will add their override via the arch case above
  ;;
```

For each arch case (`i386`, `aarch64`, `riscv`), add `sageos.h` to the override:

```
x86_64-*-sageos*)
  tm_file="${tm_file} i386/sageos.h"
  ;;
aarch64-*-sageos*)
  tm_file="${tm_file} aarch64/sageos.h"
  ;;
riscv64-*-sageos*)
  tm_file="${tm_file} riscv/sageos.h"
  ;;
```

### 4.5 Building the Cross-Compiler (on Linux host)

```bash
# Step 1: Build Binutils
mkdir build-binutils && cd build-binutils
../binutils-2.42/configure \
    --target=x86_64-sageos \
    --prefix=/opt/sageos-toolchain \
    --with-sysroot=/opt/sageos-toolchain/sysroot \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install
cd ..

# Step 2: Build GCC (stage 1 — no libc yet)
mkdir build-gcc && cd build-gcc
../gcc-14.x/configure \
    --target=x86_64-sageos \
    --prefix=/opt/sageos-toolchain \
    --with-sysroot=/opt/sageos-toolchain/sysroot \
    --enable-languages=c \
    --without-headers \
    --disable-shared \
    --disable-threads \
    --disable-libssp \
    --disable-libgomp \
    --disable-libatomic \
    --disable-libquadmath \
    --disable-nls \
    --disable-werror
make all-gcc all-target-libgcc -j$(nproc)
make install-gcc install-target-libgcc
```

Repeat this with `aarch64-sageos` and `riscv64-sageos` for the other arches.

---

## 5. Phase 2 — Newlib Port

Newlib is the right libc to port for a hobby OS. It handles everything except a small set of system calls that you provide.

### 5.1 Syscall Stubs Required

These 12 functions bridge Newlib to the SageOS kernel:

```c
/* newlib/libc/sys/sageos/syscalls.c */

#include <sys/stat.h>
#include <sys/times.h>
#include <sys/errno.h>

/* ---- write ---- */
ssize_t _write(int fd, const void *buf, size_t count) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(1 /* SYS_write */), "D"((long)fd),
          "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

/* ---- read ---- */
ssize_t _read(int fd, void *buf, size_t count) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(0 /* SYS_read */), "D"((long)fd),
          "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

/* ---- open ---- */
int _open(const char *path, int flags, int mode) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(2 /* SYS_open */), "D"(path), "S"((long)flags), "d"((long)mode)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* ---- close ---- */
int _close(int fd) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(3 /* SYS_close */), "D"((long)fd)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- lseek ---- */
off_t _lseek(int fd, off_t offset, int whence) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(8 /* SYS_lseek */), "D"((long)fd),
          "S"((long)offset), "d"((long)whence)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return (off_t)-1; }
    return (off_t)ret;
}

/* ---- fstat ---- */
int _fstat(int fd, struct stat *st) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(5 /* SYS_fstat */), "D"((long)fd), "S"(st)
        : "rcx", "r11", "memory"
    );
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- isatty ---- */
int _isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

/* ---- sbrk ---- */
void *_sbrk(intptr_t incr) {
    long cur, next;
    __asm__ volatile ("syscall" : "=a"(cur) : "0"(12), "D"(0L)
                      : "rcx", "r11");
    next = cur + incr;
    __asm__ volatile ("syscall" : "=a"(cur) : "0"(12), "D"(next)
                      : "rcx", "r11");
    if (cur < 0) { errno = ENOMEM; return (void *)-1; }
    return (void *)(cur - incr);
}

/* ---- exit ---- */
void _exit(int code) {
    __asm__ volatile (
        "syscall"
        :
        : "a"(60 /* SYS_exit */), "D"((long)code)
    );
    __builtin_unreachable();
}

/* ---- getpid ---- */
int _getpid(void) {
    return 1;   /* single-process stub */
}

/* ---- kill ---- */
int _kill(int pid, int sig) {
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

/* ---- times ---- */
clock_t _times(struct tms *buf) {
    if (buf) {
        buf->tms_utime = 0;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return 0;
}
```

> **Note for arm64 and rv64:** Replace the `syscall` inline asm blocks with the appropriate arch-specific syscall instruction and register conventions. arm64 uses `svc #0` with x8 as the syscall number and x0–x5 as args. rv64 uses `ecall` with a7 as the syscall number and a0–a5 as args.

### 5.2 Newlib Directory Layout

```
newlib/libc/sys/sageos/
├── crt0.S            ← startup code (see §6)
├── syscalls.c        ← the 12 stubs above
├── sageos-stat.h     ← struct stat layout matching kernel
└── Makefile.inc      ← register with Newlib build system
```

**`Makefile.inc`:**

```make
# newlib/libc/sys/sageos/Makefile.inc
noinst_LIBRARIES = lib.a
lib_a_SOURCES = crt0.S syscalls.c
lib_a_CFLAGS = $(AM_CFLAGS)
```

Also add `sageos` to `newlib/libc/sys/configure.in`:

```
sageos)
  sys_dir=sageos
  ;;
```

### 5.3 Building Newlib

```bash
mkdir build-newlib && cd build-newlib
../newlib-4.x/configure \
    --target=x86_64-sageos \
    --prefix=/opt/sageos-toolchain \
    --disable-newlib-supplied-syscalls \
    --enable-newlib-reent-small \
    --disable-newlib-fvwrite-in-streamio \
    --disable-newlib-wide-orient \
    --enable-newlib-nano-malloc \
    --disable-nls
make -j$(nproc)
make install
```

---

## 6. Phase 3 — C Runtime (crt0)

`crt0.S` is the first code that runs in a SageOS userspace process. It must:

1. Ensure the stack is aligned
2. Clear BSS
3. Load `argc`, `argv`, `envp`
4. Call `main()`
5. Pass the return value to `_exit()`

### 6.1 x64 — `arch/x64/newlib/sageos/crt0.S`

```asm
    .section .text
    .global _start
    .type   _start, @function

_start:
    /* Align stack to 16 bytes */
    andq    $-16, %rsp

    /* Clear BSS */
    leaq    __bss_start(%rip), %rdi
    leaq    _end(%rip), %rcx
    subq    %rdi, %rcx
    xorl    %eax, %eax
    rep stosd

    /* Load argc, argv (kernel puts them at initial rsp) */
    movq    (%rsp), %rdi          /* argc */
    leaq    8(%rsp), %rsi         /* argv */
    leaq    8(%rsi,%rdi,8), %rdx  /* envp */

    call    main

    /* Pass return value to exit */
    movl    %eax, %edi
    call    _exit
    ud2                           /* should never reach here */

    .size _start, .-_start
```

### 6.2 arm64 — `arch/arm64/newlib/sageos/crt0.S`

```asm
    .section .text
    .global _start
    .type   _start, %function

_start:
    /* Align stack */
    and     sp, sp, #~15

    /* Clear BSS */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, _end
    add     x1, x1, :lo12:_end
    sub     x2, x1, x0
    mov     x1, #0
    bl      memset

    /* Load argc, argv from stack */
    ldr     x0, [sp]              /* argc */
    add     x1, sp, #8            /* argv */
    add     x2, x1, x0, lsl #3
    add     x2, x2, #8            /* envp */

    bl      main

    /* exit with return value */
    mov     x1, x0
    mov     x8, #60               /* SYS_exit */
    svc     #0
    brk     #0                    /* unreachable */

    .size _start, .-_start
```

### 6.3 rv64 — `arch/rv64/newlib/sageos/crt0.S`

```asm
    .section .text
    .global _start
    .type   _start, @function

_start:
    /* Align stack to 16 bytes */
    andi    sp, sp, -16

    /* Clear BSS */
    la      a0, __bss_start
    la      a1, _end
    sub     a2, a1, a0
    li      a1, 0
    call    memset

    /* Load argc, argv from stack */
    lw      a0, 0(sp)             /* argc */
    addi    a1, sp, 8             /* argv */
    slli    t0, a0, 3
    add     a2, a1, t0
    addi    a2, a2, 8             /* envp */

    call    main

    /* exit */
    mv      a0, a0
    li      a7, 93               /* SYS_exit */
    ecall
    unimp                         /* unreachable */

    .size _start, .-_start
```

---

## 7. Phase 4 — libgcc

After Newlib is in place, rebuild GCC stage 2 with the full `libgcc`:

```bash
cd build-gcc
make all-target-libgcc -j$(nproc)
make install-target-libgcc
```

`libgcc` provides: software division/modulo, 64-bit arithmetic helpers on 32-bit, `__cxa_atexit` stubs, unwind tables. SageOS doesn't need exceptions initially, so pass `--disable-sjlj-exceptions` to GCC configure.

---

## 8. Phase 5 — Native GCC on SageOS (Long Term)

Running GCC natively requires SageOS to support a process model. This is a significant kernel addition beyond the syscall table.

### 8.1 Required Kernel Additions

| Feature | Minimum Implementation |
|---|---|
| `fork()` or `vfork()` | Clone task struct + copy-on-write or eager-copy of heap |
| `execve()` | Load ELF from VFS, set up new address space, jump to `_start` |
| `waitpid()` | Parent blocks until child sets exit status in task struct |
| ELF loader | Parse `PT_LOAD` segments, map into task address space |
| Separate address spaces | Either paging per-task or a simple flat offset scheme per task |

### 8.2 Minimal ELF Loader

```c
/* sageos_build/kernel/elf.c */

#include "elf.h"
#include "mm.h"
#include "vfs.h"

int elf_load(const char *path, uintptr_t *entry_out) {
    vfs_fd_t fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -ENOENT;

    Elf64_Ehdr ehdr;
    vfs_read(fd, &ehdr, sizeof(ehdr));

    /* Validate magic */
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E') {
        vfs_close(fd); return -ENOEXEC;
    }

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        vfs_seek(fd, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);
        vfs_read(fd, &phdr, sizeof(phdr));

        if (phdr.p_type != PT_LOAD) continue;

        /* Map segment into task address space */
        mm_map_region(phdr.p_vaddr, phdr.p_memsz);
        vfs_seek(fd, phdr.p_offset, SEEK_SET);
        vfs_read_at(fd, (void *)phdr.p_vaddr, phdr.p_filesz);

        /* Zero BSS portion */
        if (phdr.p_memsz > phdr.p_filesz)
            memset((void *)(phdr.p_vaddr + phdr.p_filesz),
                   0, phdr.p_memsz - phdr.p_filesz);
    }

    *entry_out = ehdr.e_entry;
    vfs_close(fd);
    return 0;
}
```

### 8.3 `execve` Syscall

```c
long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    uintptr_t entry;
    if (elf_load(path, &entry) < 0) return -ENOENT;

    task_t *t = current_task();

    /* Reset heap */
    t->heap_end = t->heap_base;

    /* Reset file descriptors (except 0, 1, 2) */
    for (int i = 3; i < MAX_FD; i++)
        if (t->fd_table[i].valid)
            vfs_close(t->fd_table[i].vfs_handle);

    /* Push argv/envp onto new stack and jump */
    setup_user_stack_and_jump(t, entry, argv, envp);
    /* Does not return */
}
```

---

## 9. sysroot Layout

The sysroot is what GCC uses to find headers and libraries when cross-compiling for SageOS:

```
/opt/sageos-toolchain/sysroot/
├── usr/
│   ├── include/         ← Newlib headers (installed by make install)
│   └── lib/
│       ├── libc.a       ← Newlib libc
│       ├── libm.a       ← Newlib libm
│       └── libgcc.a     ← GCC runtime helpers
└── lib/
    └── crt0.o           ← Startup object
```

---

## 10. Testing Milestones

### Milestone 1 — Syscall Smoke Test

Write a SageLang or C test that uses the raw syscall instruction to call `SYS_write` with a string. Verify it appears on the QEMU console. This validates the entire syscall path without needing a libc.

### Milestone 2 — Hello World via Cross-Compiler

```c
/* test/hello.c */
#include <unistd.h>
int main(void) {
    const char msg[] = "Hello from SageOS userspace!\n";
    write(1, msg, sizeof(msg) - 1);
    return 0;
}
```

```bash
x86_64-sageos-gcc -static -o hello hello.c
# Copy hello into the SageOS FAT32 image
# Boot QEMU, run /hello from the shell
# Expect: "Hello from SageOS userspace!"
```

### Milestone 3 — File I/O

Write a test that opens a file via `open()`, reads it with `read()`, and writes to stdout. Validates VFS ↔ syscall ↔ Newlib path.

### Milestone 4 — Simple programs

Port `cat`, `echo`, and a basic `printf` test. These exercise most of Newlib without touching `fork`/`exec`.

### Milestone 5 — Native GCC

Compile a minimal C program (`hello.c`) *on SageOS itself* using the natively-built GCC. This is the capstone milestone for Phase 5.

---

## 11. Key Files to Create (Summary)

| File | Description |
|---|---|
| `sageos_build/kernel/include/syscall_numbers.h` | Syscall number definitions |
| `sageos_build/kernel/syscall.c` | `syscall_dispatch()` C implementation |
| `sageos_build/kernel/include/process.h` | fd_table + heap fields in task struct |
| `sageos_build/kernel/mm.c` | `sys_brk()` |
| `arch/x64/kernel/syscall_entry.S` | x64 syscall entry (SYSCALL instruction) |
| `arch/arm64/kernel/syscall_entry.S` | arm64 syscall entry (SVC #0) |
| `arch/rv64/kernel/syscall_entry.S` | rv64 syscall entry (ECALL) |
| `gcc/config/sageos.h` | GCC OS config macros |
| `gcc/config/i386/sageos.h` | GCC x64 override |
| `gcc/config/aarch64/sageos.h` | GCC arm64 override |
| `gcc/config/riscv/sageos.h` | GCC rv64 override |
| `newlib/libc/sys/sageos/syscalls.c` | Newlib stubs (12 functions) |
| `newlib/libc/sys/sageos/crt0.S` | x64 startup (per-arch copies needed) |
| `sageos_build/kernel/elf.c` | ELF loader (Phase 5) |

---

## 12. Implementation Order for Gemini

When implementing this plan, follow this strict order:

1. **`syscall_numbers.h`** — constants only, no dependencies
2. **fd_table in `process.h`** — struct changes, extend existing task struct
3. **`sys_brk()` in `mm.c`** — simple arithmetic, needs heap fields from step 2
4. **`syscall_entry.S`** (per arch) — asm stubs calling `syscall_dispatch`
5. **`syscall.c`** — the C dispatcher, call existing VFS functions for open/read/write/close
6. **`gcc/config/sageos.h`** and arch variants — header-only changes to GCC source
7. **`newlib/libc/sys/sageos/syscalls.c`** — userspace stubs using inline asm
8. **`crt0.S`** (per arch) — startup, depends on BSS symbols from linker script
9. **Build & test** Milestone 1 and 2 before proceeding further
10. **ELF loader + `sys_execve`** only after Milestones 1–4 are green

---

## 13. Constraints and Conventions

- All kernel code is C11 (`-std=c11`), no GCC extensions in kernel headers
- SageLang (`.sage` files) should not be modified for the GCC port; this work is purely in the C kernel and the GCC/Newlib trees
- All per-arch files live under `arch/<arch>/` — do not add arch-specific code to `sageos_build/kernel/`
- The VFS API used in `syscall.c` must go through the existing VFS bridge, not call FAT32 drivers directly
- Syscall numbers must remain stable once set — treat `syscall_numbers.h` as an ABI contract
- All test binaries are static ELF executables; no dynamic linker is needed at any phase
- Build output for the toolchain goes to `/opt/sageos-toolchain/` on the host build machine; this path must not be committed to the repo
