#!/bin/bash
./sageos.sh rv64 virt build
rm -f qemu.log in.pipe
mkfifo in.pipe
# Start QEMU with serial file logging
qemu-system-riscv64 -machine virt -m 1G -display none -serial file:qemu.log -bios none -kernel build/virt_riscv64/kernel.elf < in.pipe &
QEMU_PID=$!
sleep 15
kill -9 $QEMU_PID 2>/dev/null
cat qemu.log
