#include "metal_vm.h"
#include "bootlog.h"
#include "bootinfo.h"

// Define a minimal EFI_FILE_PROTOCOL as the original is private to drivers/bootlog.c
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

// Expose global state to the VM
static EFI_FILE_PROTOCOL *g_log_file = 0;
static uint64_t g_log_offset = 0;

// Need bl_write_raw to write to log
extern void bl_write_raw(const char *buf, uint64_t len);

// Native function: bootlog_init(void)
// This initializes the bootlog using the current boot info.
extern SageOSBootInfo* console_boot_info(void);

static MetalValue native_bootlog_init(MetalVM* vm, MetalValue* args, int argc) {
    SageOSBootInfo* info = console_boot_info();
    if (info && info->log_file) {
        g_log_file = (EFI_FILE_PROTOCOL *)(uintptr_t)info->log_file;
        g_log_offset = info->log_offset;
        return mv_bool(1);
    }
    return mv_bool(0);
}

// Native function: bootlog(msg)
static MetalValue native_bootlog(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR || !g_log_file) return mv_nil();
    
    const char* msg = metal_string_get(vm, args[0].as.str_idx);
    uint64_t len = 0;
    while (msg[len]) len++;
    
    // Inline the write logic here or call a helper
    // For now, I'll assume we can call the C implementation of bl_write_raw
    extern void bl_write_raw(const char *buf, uint64_t len);
    bl_write_raw(msg, len);
    return mv_nil();
}

void register_bootlog_native_bindings(MetalVM* vm) {
    metal_vm_register_native(vm, "bootlog_init", native_bootlog_init);
    metal_vm_register_native(vm, "bootlog", native_bootlog);
}
