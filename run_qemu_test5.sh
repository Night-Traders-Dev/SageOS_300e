#!/bin/bash
rm -f qemu.log
qemu-system-riscv64 -machine virt -m 1G -display none -serial file:qemu.log -bios none -kernel build/virt_riscv64/kernel.elf &
QEMU_PID=$!
sleep 20
kill -9 $QEMU_PID 2>/dev/null
cat qemu.log
