# =============================================================================
# SageOS Power Management - Pure SageLang Port
# power.sage
#
# C source kept: sageos_build/kernel/drivers/power.c (unchanged, regression baseline)
#
# All hardware I/O (outb 0x501, outb 0x64, HLT loop, ACPI S3/S5) is delegated
# to the C driver via VM bindings. This module handles the interactive shell
# layer: confirmation messages, dmesg logging, and dispatch.
# =============================================================================

proc cmd_shutdown():
    os_write_str("\nRequesting ACPI S5 poweroff...")
    os_write_str("\n")
    os_shutdown()
end

proc cmd_poweroff():
    cmd_shutdown()
end

proc cmd_suspend():
    os_write_str("\nRequesting ACPI S3 suspend...")
    os_write_str("\n")
    os_suspend()
end

proc cmd_halt():
    os_write_str("\nHalting.")
    os_write_str("\n")
    os_halt()
end

proc cmd_reboot():
    os_write_str("\nRebooting via i8042...")
    os_write_str("\n")
    os_reboot()
end

proc cmd_exit():
    if os_is_qemu() != 0:
        os_write_str("\nExiting QEMU...")
        os_write_str("\n")
        os_qemu_exit()
    else:
        os_write_str("\nExit: calling ACPI S5 shutdown...")
        os_write_str("\n")
        os_shutdown()
    end
end
