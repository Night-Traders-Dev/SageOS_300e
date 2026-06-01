#ifndef _SAGEOS_BOOT_STAGES_H
#define _SAGEOS_BOOT_STAGES_H

#include <stdint.h>

/**
 * SageOS Boot Stages as defined in the Core Systems Architecture Specification.
 */
typedef enum {
    STAGE_0_FIRMWARE,           // Platform firmware (UEFI, OpenSBI, etc.)
    STAGE_1_EARLY_MM,           // Early memory management and paging
    STAGE_2_IRQ_INIT,           // Interrupt controller and basic traps
    STAGE_3_DEVICE_DISCOVERY,   // Bus scanning and basic console
    STAGE_4_STORAGE_VFS,        // Storage drivers and VFS mounting
    STAGE_5_RUNTIME_BRINGUP,    // SageLang runtime and SGVM core
    STAGE_6_SERVICE_ACTIVATION, // Runtime Manager and system services
    STAGE_7_USERSPACE_SESSION,  // PID 1 and shell/UI
    STAGE_HALT                  // System shutdown/halt
} SageOSBootStage;

void sageos_set_boot_stage(SageOSBootStage stage);
SageOSBootStage sageos_get_boot_stage(void);

#endif
