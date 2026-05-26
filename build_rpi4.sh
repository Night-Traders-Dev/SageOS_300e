#!/bin/sh
set -e
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    AS="aarch64-linux-gnu-as"
    ASFLAGS=""
    CC="aarch64-linux-gnu-gcc"
    LD="aarch64-linux-gnu-ld"
else
    AS="clang -target aarch64-unknown-elf -c"
    ASFLAGS=""
    CC="clang -target aarch64-unknown-elf"
    LD="ld.lld"
fi
echo 'Building rpi4 kernel...'
echo '  $AS $ASFLAGS -o boot.o rpi4_boot_demo/boot.S'
$AS $ASFLAGS -o boot.o rpi4_boot_demo/boot.S
echo '  $CC -ffreestanding -nostdlib -mgeneral-regs-only -c -o kernel.o rpi4_boot_demo/kernel.c'
$CC -ffreestanding -nostdlib -mgeneral-regs-only -c -o kernel.o rpi4_boot_demo/kernel.c
echo '  $LD -z max-page-size=4096 -T rpi4_boot_demo/linker.ld -o rpi4_boot_demo/kernel.elf boot.o kernel.o'
$LD -z max-page-size=4096 -T rpi4_boot_demo/linker.ld -o rpi4_boot_demo/kernel.elf boot.o kernel.o
echo 'Build complete: rpi4_boot_demo/kernel.elf'
echo 'Run with:'
echo '  qemu-system-aarch64 -machine raspi4b -cpu cortex-a72 -m 4G -display none -serial stdio -kernel rpi4_boot_demo/kernel.elf'
