#!/bin/bash
rm -f qemu.log in.pipe
mkfifo in.pipe
qemu-system-riscv64 -machine virt -m 1G -display none -serial file:qemu.log -bios none -kernel build/virt_riscv64/kernel.elf < in.pipe &
QEMU_PID=$!
sleep 2
echo "a" > in.pipe
sleep 2
echo "b" > in.pipe
sleep 2
kill -9 $QEMU_PID 2>/dev/null
cat qemu.log
