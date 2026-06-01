# =============================================================================
# SageOS Kernel Info Display — Pure SageLang Port
# kernel_info.sage
#
# C source kept: sageos_build/kernel/core/kernel.c (UNCHANGED — C bootstrap)
#
# kmain() is called from entry.S before the SageLang VM is initialised.
# It creates the VM itself via sage_init_run(). It cannot move to Sage.
#
# This module provides a Sage-native reproduction of the kernel banner and
# subsystem state, readable at any time from the interactive shell.
# It also demonstrates os_dmesg_log() — Sage writing to the kernel ring buffer.
# =============================================================================

proc cmd_kernel():
    os_dmesg_log("kernel_info: display requested from SageLang")

    os_write_str("\nSageOS Kernel Information:")
    os_write_str("\n  version:   ")
    os_write_str(os_version_string())

    os_write_str("\n  arch:      x86_64 freestanding ELF")
    os_write_str("\n  entry:     kmain() via entry.S [C bootstrap]")

    os_write_str("\n  keyboard:  ")
    os_write_str(os_input_backend())

    os_write_str("\n  cpus:      ")
    os_write_str(os_num_to_str(os_sched_cpu_count()))

    os_write_str("\n  threads:   ")
    os_write_str(os_num_to_str(os_sched_thread_count()))

    os_write_str("\n  uptime:    ")
    os_write_str(os_uptime_str())

    os_write_str("\n")
    os_write_str("\nMemory:")
    os_write_str("\n  ram used:  ")
    os_write_str(os_ram_used_str())
    os_write_str("\n  ram total: ")
    os_write_str(os_ram_total_str())

    os_write_str("\n")
    os_write_str("\nSubsystems (all initialised in kmain before Sage VM):")
    os_write_str("\n  [x] serial    [x] console   [x] acpi")
    os_write_str("\n  [x] smp       [x] battery   [x] ramfs+vfs")
    os_write_str("\n  [x] ata       [x] pci       [x] sdhci")
    os_write_str("\n  [x] net       [x] keyboard  [x] status")
    os_write_str("\n  [x] dmesg     [x] scheduler [x] sage-vm")

    os_write_str("\n")
    os_write_str("\nFramebuffer:")
    if os_fb_available() != 0:
        os_write_str("\n  base:       ")
        os_write_str(os_fb_base_str())
        os_write_str("\n  resolution: ")
        os_write_str(os_fb_width_str())
        os_write_str("x")
        os_write_str(os_fb_height_str())
    else:
        os_write_str("\n  not available (serial-only mode)")
    end

    os_write_str("\n")
    os_write_str("\nSwap:")
    if os_swap_available() != 0:
        os_write_str("\n  partition 3, 125 MiB — active")
    else:
        os_write_str("\n  not available (no ATA device)")
    end

    os_write_str("\n")
end
