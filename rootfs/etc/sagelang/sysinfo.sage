# sysinfo.sage - Pure Sage sysinfo command

proc main():
    os_write_str("\nSystem Info:\n  Platform: Virtual Bare-metal (rv64/virt)\n")
    
    let mem = os_get_mem_stats()
    if mem != nil:
        let total_mb = mem["total"] / (1024 * 1024)
        os_write_str("  RAM: " + os_num_to_str(total_mb) + " MB\n")
    end
    
main()
