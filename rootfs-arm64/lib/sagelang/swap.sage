# swap.sage - Pure Sage memory utilization visualizer

proc main():
    let mem = os_get_mem_stats()
    if mem == nil:
        os_write_str("\nError: Could not fetch memory stats")
        return

    let used = mem["used"]
    let total = mem["total"]
    let free = total - used
    
    let pct = 0
    if total > 0:
        pct = (used * 100) / total
    
    os_write_str("\n\033[1;33mMemory Utilization\033[0m")
    os_write_str("\nUsed:  " + os_num_to_str(used / 1024 / 1024) + " MB")
    os_write_str("\nFree:  " + os_num_to_str(free / 1024 / 1024) + " MB")
    os_write_str("\nTotal: " + os_num_to_str(total / 1024 / 1024) + " MB")
    
    # Progress bar
    let bar_width = 40
    let filled = 0
    if total > 0:
        filled = (used * bar_width) / total
    
    let bar = "["
    let j = 0
    while j < bar_width:
        if j < filled: bar = bar + "#"
        else: bar = bar + "-"
        j = j + 1
    bar = bar + "] " + os_num_to_str(pct) + "%"
    
    os_write_str("\n\n" + bar + "\n")

main()
