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
        boot_asm = boot_asm + bb.generate_serial_boot_x86()
    end
    if arch == "aarch64":
        boot_asm = start.emit_start_aarch64("kmain", "stack_top")
        boot_asm = boot_asm + bb.generate_serial_boot_aarch64()
    end
    if arch == "riscv64":
        boot_asm = start.emit_start_riscv64("kmain", "stack_top")
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
        "sageos_build/kernel/fs/fat32.c",
        "sageos_build/kernel/fs/elf.c",
        "sageos_build/kernel/fs/json.c",
        "sageos_build/kernel/shell/shell.c",
        "sageos_build/kernel/shell/shell_helper.c"
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

    script = script + "CFLAGS=\"-ffreestanding -nostdlib -fno-stack-protector -fno-pie -mno-red-zone -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -DSAGE_BARE_METAL -O2\"" + NL
    if arch == "aarch64": script = script + "CFLAGS=\"-ffreestanding -nostdlib -fno-stack-protector -fno-pie -mgeneral-regs-only -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -DSAGE_BARE_METAL -O2\"" + NL end
    if arch == "riscv64": script = script + "CFLAGS=\"-ffreestanding -nostdlib -fno-stack-protector -fno-pie -mcmodel=medany -Isageos_build/kernel/include -Isageos_build/kernel/core/sagelang -Isageos_build/actual_sagelang_build -DSAGE_BARE_METAL -O2\"" + NL end

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
    script = script + "$CC -nostdlib -static -fno-pie -no-pie -z max-page-size=4096 -T " + linker_path + " -o " + elf_path + " " + objects_str + NL
    script = script + "echo 'Build complete: " + elf_path + "'" + NL
    
    io.writefile(output_dir + "/build.sh", script)
    print "  > Generated " + output_dir + "/build.sh"
end

for a in architectures:
    generate_virt_build(a)
end
