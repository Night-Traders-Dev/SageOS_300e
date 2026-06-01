#!/bin/bash
./sageos.sh x64 virt build
rm -f qemu.log in.pipe
mkfifo in.pipe
qemu-system-x86_64 -machine q35 -m 1G -display none -serial file:qemu.log -kernel build/virt_x86_64/kernel.elf < in.pipe &
QEMU_PID=$!
sleep 2
echo "a" > in.pipe
sleep 2
kill -9 $QEMU_PID 2>/dev/null
cat qemu.log
