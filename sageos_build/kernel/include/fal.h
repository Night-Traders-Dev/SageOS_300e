#ifndef _SAGEOS_FAL_H
#define _SAGEOS_FAL_H

#include <stdint.h>
#include "bootinfo.h"

/**
 * Firmware Abstraction Layer (FAL)
 * Standardizes interface between kernel and platform firmware.
 */

typedef struct {
    const char* firmware_vendor;
    uint32_t revision;
    
    // Memory Management
    void* (*get_memory_map)(void);
    
    // System Control
    void (*reboot)(void);
    void (*poweroff)(void);
    
    // Graphics
    void* (*get_framebuffer)(void);
} FirmwareOps;

extern FirmwareOps* g_fal_ops;

void fal_init(SageOSBootInfo* info);

#endif
