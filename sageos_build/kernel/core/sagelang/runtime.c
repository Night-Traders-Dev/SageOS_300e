/*
 * runtime.c — SageLang Native MetalVM Runtime for SageOS
 *
 * This implementation integrates the official freestanding MetalVM
 * from the SageLang project, replacing the previous custom runtime.
 */

#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "io.h"
#include "sage_alloc.h"
#include "metal_vm.h"
#include "sage_libc_shim.h"

/* --- MetalVM I/O Callbacks --- */

static void vm_write_char(char c) {
    console_putc(c);
}

static int vm_read_char(void) {
    /* Non-blocking read for now */
    KeyEvent ev;
    if (keyboard_poll_event(&ev) && ev.pressed && !ev.extended) {
        return (int)ev.ascii;
    }
    return -1;
}

static void vm_write_port(int port, int val) {
    outb((uint16_t)port, (uint8_t)val);
}

static int vm_read_port(int port) {
    return (int)inb((uint16_t)port);
}

/* --- Runtime API --- */

void sage_runtime_init(void) {
    /* Heap and other components already initialized by kmain or shim */
}

void sage_execute(const char *module_name) {
    if (!module_name || !*module_name) {
        console_write("\nSageLang Native MetalVM v3.4.1 (Freestanding)");
        console_write("\nStatus: Official high-performance runtime active");
        console_write("\nMemory: 1MB Arena available");
        console_write("\n");
        console_write("\nPlease use precompiled .sagec files.");
        console_write("\nExample: sage hello.sagec\n");
        return;
    }

    /* Find bytecode in RAMFS */
    const char *data = NULL;
    uint64_t size = ramfs_find_size(module_name, &data);

    if (!data) {
        /* Try /etc/ prefix if not found directly */
        char path[128];
        sage_snprintf(path, sizeof(path), "/etc/%s", module_name);
        size = ramfs_find_size(path, &data);
    }

    if (!data) {
        console_write("\nsage: bytecode file not found: ");
        console_write(module_name);
        console_write("\n");
        return;
    }

    console_write("\nsage: loading native bytecode (");
    console_u32((uint32_t)size);
    console_write(" bytes)...\n");

    /* Reset arena before VM run */
    sage_arena_reset();

    /* Allocate VM on SageLang heap to avoid stack overflow */
    MetalVM *vm = (MetalVM *)sage_malloc(sizeof(MetalVM));
    if (!vm) {
        console_write("sage: failed to allocate VM state (arena full)\n");
        return;
    }

    /* Initialize VM */
    metal_vm_init(vm);

    /* Setup callbacks */
    vm->write_char = vm_write_char;
    vm->read_char = vm_read_char;
    vm->write_port = vm_write_port;
    vm->read_port = vm_read_port;

    /* Load and run */
    metal_vm_load(vm, (const unsigned char *)data, (int)size);
    int status = metal_vm_run(vm);

    if (status != 0) {
        console_write("\nsage: VM error: ");
        if (vm->error_msg) console_write(vm->error_msg);
        else console_write("halted");
        console_write("\n");
    } else {
        console_write("\nsage: execution finished successfully.\n");
    }
}

int sage_compile(const char *src_path, const char *out_path) {
    (void)src_path; (void)out_path;
    console_write("\nsage: kernel compiler disabled to minimize footprint.");
    console_write("\nPlease compile on host: ./sagelang --emit-vm src.sage -o out.sagec\n");
    return -1;
}

void sage_repl(void) {
    /* The REPL in SageOS 0.1.2 is a simple line-reader.
       Since we don't have a kernel-resident compiler anymore,
       we show info and return. */
    sage_execute(NULL);
}
