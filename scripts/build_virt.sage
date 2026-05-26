import io
import sys
import os.boot.build as bb
import os.boot.start as start
import os.boot.linker as linker

let architectures = ["x86_64", "aarch64", "riscv64"]
let NL = chr(10)

proc generate_virt_build(arch):
    let output_dir = "build/virt_" + arch
    sys.exec("mkdir -p " + output_dir)
    
    print "Generating build for " + arch + " in " + output_dir + "..."
    
    # 1. Generate boot assembly with serial support
    let boot_asm = ""
    if arch == "x86_64":
        boot_asm = start.generate_boot_asm(nil)
        boot_asm = replace(boot_asm, ".section .multiboot, \"a\"", ".section .multiboot, \"a\"" + NL + ".align 4" + NL + "multiboot1_header:" + NL + "	.long 0x1BADB002" + NL + "	.long 0x00000003" + NL + "	.long -(0x1BADB002 + 0x00000003)" + NL)
        boot_asm = replace(boot_asm, "# Clear page tables", ".section .text" + NL + "\t# Clear page tables")
        boot_asm = replace(boot_asm, "long_mode_start:", "long_mode_start:" + NL + "	# Enable SSE" + NL + "	movq %cr0, %rax" + NL + "	andq $0xFFFFFFFFFFFFFFFB, %rax" + NL + "	orq $0x2, %rax" + NL + "	movq %rax, %cr0" + NL + "	movq %cr4, %rax" + NL + "	orq $0x600, %rax" + NL + "	movq %rax, %cr4")
        boot_asm = boot_asm + bb.generate_serial_boot_x86()
    end
    if arch == "aarch64":
        boot_asm = start.emit_start_aarch64("kmain", "stack_top")
        boot_asm = replace(boot_asm, "mov sp, x0", "mov sp, x0" + NL + "	# Enable FPU/NEON" + NL + "	mov x0, #(3 << 20)" + NL + "	msr cpacr_el1, x0" + NL + "	# Disable alignment checking" + NL + "	mrs x0, sctlr_el1" + NL + "	bic x0, x0, #2" + NL + "	msr sctlr_el1, x0" + NL + "	isb")
        boot_asm = boot_asm + bb.generate_serial_boot_aarch64()
    end
    if arch == "riscv64":
        boot_asm = start.emit_start_riscv64("kmain", "stack_top")
        boot_asm = replace(boot_asm, "csrci mstatus, 0x8", "csrci mstatus, 0x8" + NL + "	# Enable FPU (set mstatus.FS = 11)" + NL + "	li t0, 0x6000" + NL + "	csrs mstatus, t0")
        boot_asm = boot_asm + bb.generate_serial_boot_riscv64()
    end

    # 2. Generate linker script
    let ld_config = linker.default_config()
    if arch == "aarch64":
        ld_config["base_address"] = 0x40000000 
    end
    if arch == "riscv64":
        ld_config["base_address"] = 0x80000000
    end
    let linker_script = linker.generate_script(ld_config)

    # 3. Write artifacts
    let boot_path = output_dir + "/boot.S"
    let linker_path = output_dir + "/linker.ld"
    io.writefile(boot_path, boot_asm)
    io.writefile(linker_path, linker_script)

    # 4. List C sources
    let c_sources = [
        "sageos_build/kernel/core/virt_main.c",
        "sageos_build/kernel/core/virt_console.c",
        "sageos_build/kernel/core/virt_keyboard.c",
        "sageos_build/kernel/core/sagelang/sage_libc_shim.c",
        "sageos_build/kernel/fs/vfs.c",
        "sageos_build/kernel/fs/ramfs.c",
        "sageos_build/kernel/core/kernel_stubs.c",
        "sageos_build/kernel/shell/shell.c",
        "sageos_build/kernel/shell/shell_helper.c",
        "sageos_build/kernel/shell/extra_cmds.c",
        "sageos_build/kernel/shell/sage_shell_entry.c",
        "sageos_build/kernel/core/sagelang/metal_vm.c"
    ]

    # 5. Construct build script
    let script = "#!/bin/sh" + NL + "set -e" + NL
    
    if arch == "x86_64":
        script = script + "AS=\"x86_64-linux-gnu-as\"; ASFLAGS=\"--64\"; CC=\"x86_64-linux-gnu-gcc\"; LD=\"x86_64-linux-gnu-ld\"" + NL
    end
    if arch == "aarch64":
        script = script + "AS=\"aarch64-linux-gnu-as\"; ASFLAGS=\"\"; CC=\"aarch64-linux-gnu-gcc\"; LD=\"aarch64-linux-gnu-ld\"" + NL
    end
    if arch == "riscv64":
        script = script + "AS=\"riscv64-linux-gnu-as\"; ASFLAGS=\"-march=rv64gc -mabi=lp64d\"; CC=\"riscv64-linux-gnu-gcc\"; LD=\"riscv64-linux-gnu-ld\"" + NL
    end

    script = script + "CFLAGS=\"-ffreestanding -nostdinc -fno-stack-protector -fno-pie -mno-red-zone -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -Isageos_build/actual_sagelang_build/libc -Isageos_build/sage_lang/core/include -Isageos_build/sage_lang/core/include/vm -DSAGE_BARE_METAL -O2\"" + NL
    if arch == "aarch64": script = script + "CFLAGS=\"-ffreestanding -nostdinc -fno-stack-protector -fno-pie -mstrict-align -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -Isageos_build/actual_sagelang_build/libc -Isageos_build/sage_lang/core/include -Isageos_build/sage_lang/core/include/vm -DSAGE_BARE_METAL -O2\"" + NL end
    if arch == "riscv64": script = script + "CFLAGS=\"-ffreestanding -nostdinc -fno-stack-protector -fno-pie -mcmodel=medany -march=rv64g -mabi=lp64d -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -Isageos_build/actual_sagelang_build/libc -Isageos_build/sage_lang/core/include -Isageos_build/sage_lang/core/include/vm -DSAGE_BARE_METAL -O2\"" + NL end

    script = script + "echo 'Building SageOS Virt (" + arch + ")...'" + NL
    script = script + "$AS $ASFLAGS -o " + output_dir + "/boot.o " + boot_path + NL
    
    let objects_str = output_dir + "/boot.o"
    let i = 0
    while i < len(c_sources):
        let src = c_sources[i]
        let obj = output_dir + "/obj" + str(i) + ".o"
        script = script + "echo '  CC " + src + "'" + NL
        script = script + "$CC $CFLAGS -c -o " + obj + " " + src + NL
        objects_str = objects_str + " " + obj
        i = i + 1
    end

    let elf_path = output_dir + "/kernel.elf"
    script = script + "echo '  LD " + elf_path + "'" + NL
    let ld_flags = "-nostdlib -static -fno-pie -no-pie -z max-page-size=4096"
    if arch == "x86_64":
        ld_flags = ld_flags + " -Wl,--oformat,elf32-i386"
    end
    script = script + "$CC " + ld_flags + " -T " + linker_path + " -o " + elf_path + " " + objects_str + NL
    script = script + "echo 'Build complete: " + elf_path + "'" + NL
    
    io.writefile(output_dir + "/build.sh", script)
    print "  > Generated " + output_dir + "/build.sh"
end

for a in architectures:
    generate_virt_build(a)
end
