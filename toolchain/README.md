# SageOS GCC Configuration Patches

This directory contains the necessary configuration files to add SageOS support to GCC.

## Integration Instructions

### 1. Copy Files
Copy the contents of `toolchain/gcc/config/` into your GCC source tree's `gcc/config/` directory.

### 2. Modify `gcc/config.gcc`
Add the following entries to the `case ${target} in` block in `gcc/config.gcc`:

```bash
# Generic SageOS support
*-*-sageos*)
  tm_file="${tm_file} sageos.h"
  tmake_file="${tmake_file} t-slibgcc"
  ;;

# x86_64 SageOS
x86_64-*-sageos*)
  tm_file="${tm_file} i386/sageos.h"
  ;;

# AArch64 SageOS
aarch64-*-sageos*)
  tm_file="${tm_file} aarch64/sageos.h"
  ;;

# RISC-V 64 SageOS
riscv64-*-sageos*)
  tm_file="${tm_file} riscv/sageos.h"
  ;;
```

### 3. Build Binutils
Binutils also needs to know about the `*-*-sageos*` target. You should update `config.sub` in the root of the source tree to recognize `sageos` as a valid OS.

```bash
# In config.sub, find the case statement for OSes and add:
sageos*)
    ;;
```

And in `bfd/config.bfd`:

```bash
# x86_64 SageOS
x86_64-*-sageos*)
    targ_defvec=x86_64_elf64_vec
    targ_selvecs="i386_elf32_vec iamcu_vec x86_64_elf32_vec"
    want64=true
    ;;
```

### 4. Build Newlib
Newlib needs to be configured with `--target=*-*-sageos`. The syscall stubs in `newlib/libc/sys/sageos/syscalls.c` and the unified `crt0.S` will provide the necessary interface to the SageOS kernel.

The following syscalls are now supported by the SageOS kernel for Newlib:
- read, write, open, close, lseek, fstat
- brk, exit, getpid, gettimeofday, nanosleep
- waitpid, times, isatty (basic)
