// ============================================================================
// Battery Driver (SageLang)
// ============================================================================

proc init_battery():
    battery_init()
end

proc get_battery_pct():
    return battery_percent()
end
