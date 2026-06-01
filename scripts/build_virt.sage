import io
import sys
import os.boot.build as bb
import os.boot.start as start
import os.boot.linker as linker
import scripts.toml as toml

let architectures = ["x86_64", "aarch64", "riscv64"]
let NL = chr(10)

# Load build.toml configuration
let toml_content = io.readfile("build.toml")
let build_config = toml.parse_toml(toml_content)

# 0. Generate version header from root VERSION source
let version = io.readfile("VERSION")
version = replace(version, chr(10), "")
version = replace(version, chr(13), "")

let v_content = "#define SAGEOS_VERSION \"" + version + "\"" + NL
v_content = v_content + "/* ABI Versioning for Runtime/Kernel compatibility */" + NL
v_content = v_content + "#define SAGE_ABI_MAJOR 0" + NL
v_content = v_content + "#define SAGE_ABI_MINOR 4" + NL

io.writefile("sageos_build/kernel/include/version.h", v_content)
print "Generated version.h (v" + version + ") using declarative build.toml"

# Regenerate command embeddings
sys.exec("python3 sageos_build/kernel/fs/embed_commands.py sageos_build/kernel/etc sageos_build/kernel/fs/commands_embed.h")
print "Regenerated sageos_build/kernel/fs/commands_embed.h"

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
    
    # Fix linker script segments (ensure data/rodata/bss are in a LOAD segment)
    linker_script = replace(linker_script, "*(.rodata .rodata.*)", "*(.rodata .rodata.* .srodata .srodata.*)")
    linker_script = replace(linker_script, "*(.data .data.*)", "*(.data .data.* .sdata .sdata.* .got .got.*)")
    linker_script = replace(linker_script, "*(.bss .bss.*)", "*(.bss .bss.* .sbss .sbss.*)")

    linker_script = replace(linker_script, ".rodata ALIGN(4096) :" + NL + "	{" + NL + "		*(.rodata .rodata.* .srodata .srodata.*)" + NL + "	}", ".rodata ALIGN(4096) :" + NL + "	{" + NL + "		*(.rodata .rodata.* .srodata .srodata.*)" + NL + "	} :text")
    linker_script = replace(linker_script, ".data ALIGN(4096) :" + NL + "	{" + NL + "		*(.data .data.* .sdata .sdata.* .got .got.*)" + NL + "	}", ".data ALIGN(4096) :" + NL + "	{" + NL + "		*(.data .data.* .sdata .sdata.* .got .got.*)" + NL + "	} :text")
    linker_script = replace(linker_script, ".bss ALIGN(4096) :" + NL + "	{" + NL + "		__bss_start = .;" + NL + "		*(.bss .bss.* .sbss .sbss.*)" + NL + "		*(COMMON)" + NL + "		__bss_end = .;" + NL + "	}", ".bss ALIGN(4096) :" + NL + "	{" + NL + "		__bss_start = .;" + NL + "		*(.bss .bss.* .sbss .sbss.*)" + NL + "		*(COMMON)" + NL + "		__bss_end = .;" + NL + "	} :text")

    # 3. Write artifacts
    boot_asm = replace(boot_asm, "stack_bottom:", ".global stack_bottom" + NL + "stack_bottom:")
    let boot_path = output_dir + "/boot.S"
    let linker_path = output_dir + "/linker.ld"
    io.writefile(boot_path, boot_asm)
    io.writefile(linker_path, linker_script)

    # 4. List sources (Read from declarative build.toml)
    let c_sources = []
    let common_sources = build_config["sources"]["common"]
    let i = 0
    while i < len(common_sources):
        let _u = push(c_sources, common_sources[i])
        i = i + 1
    end

    # Retrieve target specific config from build.toml
    let target_section = "targets." + arch
    let target_config = build_config[target_section]
    
    let extra_sources = target_config["extra_sources"]
    let j = 0
    while j < len(extra_sources):
        let _u = push(c_sources, extra_sources[j])
        j = j + 1
    end

    let target_as = target_config["as"]
    let target_asflags = target_config["asflags"]
    let target_cc = target_config["cc"]
    let target_ld = target_config["ld"]
    let target_cflags = target_config["cflags"]

    # 5. Construct build script
    let script = "#!/bin/sh" + NL + "set -e" + NL
    script = script + "AS=\"" + target_as + "\"; ASFLAGS=\"" + target_asflags + "\"; CC=\"" + target_cc + "\"; LD=\"" + target_ld + "\"" + NL
    script = script + "CFLAGS=\"" + target_cflags + "\"" + NL

    script = script + "echo 'Building SageOS Virt (" + arch + ")...'" + NL
    script = script + "$AS $ASFLAGS -o " + output_dir + "/boot.o " + boot_path + NL
    
    let objects_str = output_dir + "/boot.o"
    let k = 0
    while k < len(c_sources):
        let src = c_sources[k]
        let obj = output_dir + "/obj" + str(k) + ".o"
        script = script + "echo '  CC " + src + "'" + NL
        if src[len(src)-2:len(src)] == ".S":
            script = script + "$CC $CFLAGS -c -o " + obj + " " + src + NL
        else:
            if contains(src, "sage_lang"):
                script = script + "$CC $CFLAGS -include sage_libc_shim.h -include sage_port.h -O2 -c -o " + obj + " " + src + NL
            else:
                script = script + "$CC $CFLAGS -include sage_libc_shim.h -O2 -c -o " + obj + " " + src + NL
            end
        end
        objects_str = objects_str + " " + obj
        k = k + 1
    end

    let elf_path = output_dir + "/kernel.elf"
    script = script + "echo '  LD " + elf_path + "'" + NL
    let ld_flags = "-nostdlib -static -fno-pie -no-pie -z max-page-size=4096"
    if arch == "x86_64":
        ld_flags = ld_flags + " -Wl,--oformat,elf32-i386"
    end
    script = script + "$CC " + ld_flags + " -T " + linker_path + " -o " + elf_path + " " + objects_str + NL
    
    # 6. Basic ELF Validation
    script = script + "if command -v readelf >/dev/null; then" + NL
    script = script + "  echo '  Validating " + elf_path + "...'" + NL
    script = script + "  readelf -h " + elf_path + " | grep -q \"Entry point address:\" || (echo \"[FAIL] No entry point found in " + elf_path + "\"; exit 1)" + NL
    script = script + "  echo '  [OK] ELF header validated.'" + NL
    script = script + "fi" + NL
    
    script = script + "echo 'Build complete: " + elf_path + "'" + NL
    
    io.writefile(output_dir + "/build.sh", script)
    print "  > Generated " + output_dir + "/build.sh"
end

for a in architectures:
    generate_virt_build(a)
end
