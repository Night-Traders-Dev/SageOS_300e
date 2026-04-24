#ifndef SAGEOS_BOOTINFO_H
#define SAGEOS_BOOTINFO_H

#include <stdint.h>

#define SAGEOS_BOOT_MAGIC 0x534147454F534249ULL

typedef struct {
    uint64_t magic;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
    uint32_t reserved;

    uint64_t system_table;
    uint64_t boot_services;
    uint64_t runtime_services;
    uint64_t con_in;
    uint64_t con_out;
    uint32_t boot_services_active;
    uint32_t input_mode;
    uint64_t acpi_rsdp;

    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_desc_size;
    uint64_t memory_total;
    uint64_t memory_usable;

    uint64_t kernel_base;
    uint64_t kernel_size;
} SageOSBootInfo;

#endif
