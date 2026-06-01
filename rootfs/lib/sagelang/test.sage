# =============================================================================
# SageOS System Diagnostic Test Script
# /etc/commands/test.sage
# =============================================================================

proc main():
    os_write_char(10)
    os_write_str("--- Starting SageOS Diagnostic Test ---")
    os_write_char(10)

    # 1. System Info
    os_write_str("System: ")
    os_write_str(os_version_string())
    os_write_char(10)

    # 2. CPU Metric
    os_write_str("CPU Load: ")
    os_write_str(os_num_to_str(os_cpu_percent()))
    os_write_str("%")
    os_write_char(10)

    # 3. CPU Core Count
    os_write_str("Logical Cores: ")
    os_write_str(os_num_to_str(os_smp_cpu_count()))
    os_write_char(10)

    # 4. Memory Usage
    os_write_str("Memory: ")
    os_write_str(os_ram_used_str())
    os_write_str(" / ")
    os_write_str(os_ram_total_str())
    os_write_char(10)

    # 5. Battery
    os_write_str("Battery: ")
    os_write_str(os_num_to_str(os_battery_percent()))
    os_write_str("%")
    os_write_char(10)

    os_write_str("--- Diagnostic Test Complete ---")
    os_write_char(10)

main()
