# SageOS Status Bar and System Metrics
# status.sage

proc status_refresh():
    let bat = os_battery_percent()
    let cpu = os_cpu_percent()
    let ram = 0

    let used_mb = os_ram_used_mb()
    let total_mb = os_ram_total_mb()
    if total_mb > 0:
        ram = (used_mb * 100) / total_mb
    end

    let bat_str = "--"
    if bat >= 0:
        bat_str = os_num_to_str(bat)
    end

    let cpu_str = "--"
    if cpu >= 0:
        cpu_str = os_num_to_str(cpu)
    end

    let ram_str = "--"
    if ram >= 0:
        ram_str = os_num_to_str(ram)
    end

    let text = "BAT " + bat_str + "%  CPU " + cpu_str + "%  RAM " + ram_str + "%"
end

proc cmd_status():
    let bat = os_battery_percent()
    let cpu = os_cpu_percent()
    let total_mb = os_ram_total_mb()
    let used_mb = os_ram_used_mb()

    os_write_str("\nStatus:")

    os_write_str("\n  battery: ")
    if bat >= 0:
        os_write_str(os_num_to_str(bat))
        os_write_str("%")
    else:
        os_write_str("unavailable until ACPI battery AML/EC query")
    end

    os_write_str("\n  cpu: ")
    if cpu >= 0:
        os_write_str(os_num_to_str(cpu))
        os_write_str("%")
    else:
        os_write_str("unavailable")
    end

    os_write_str("\n  ram: ")
    if total_mb > 0:
        let ram_pct = (used_mb * 100) / total_mb
        os_write_str(os_num_to_str(ram_pct))
        os_write_str("% reserved/used by early firmware/kernel view")
    else:
        os_write_str("unavailable, no UEFI memory summary")
    end
    os_write_str("\n")
end

