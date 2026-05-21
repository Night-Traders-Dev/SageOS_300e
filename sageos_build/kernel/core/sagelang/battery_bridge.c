#include "metal_vm.h"
#include "battery.h"
#include "console.h"

// Native function: battery_percent(void)
static MetalValue native_battery_percent(MetalVM* vm, MetalValue* args, int argc) {
    return mv_num(battery_percent());
}

// Native function: battery_init(void)
static MetalValue native_battery_init(MetalVM* vm, MetalValue* args, int argc) {
    battery_init();
    return mv_nil();
}

void register_battery_native_bindings(MetalVM* vm) {
    metal_vm_register_native(vm, "battery_percent", native_battery_percent);
    metal_vm_register_native(vm, "battery_init", native_battery_init);
}
