// ============================================================================
// Status Driver (SageLang)
// ============================================================================

proc refresh_status():
    status_refresh()
end

proc get_ram_total():
    return ram_total_bytes()
end

proc get_ram_used():
    return ram_used_bytes()
end
