# =============================================================================
# SageOS dmesg implementation
# dmesg.sage
# =============================================================================

proc dmesg_dump_sage():
    let total = os_dmesg_get_total()
    let size = os_dmesg_get_size()
    let head = os_dmesg_get_head()
    
    let start = 0
    let count = 0
    
    if total < size:
        start = 0
        count = total
    else:
        start = head
        count = size
        
    let i = 0
    os_write_char(10) # Newline
    while i < count:
        let idx = (start + i) % size
        let c = os_dmesg_get_char(idx)
        if c != 0:
            os_write_char(c)
        i = i + 1

# Export for shell dispatch if needed
proc cmd_dmesg():
    dmesg_dump_sage()
