#ifndef _SAGEOS_BOOT_STAGES_H
#define _SAGEOS_BOOT_STAGES_H

#include <stdint.h>

/**
 * SageOS Boot Stages as defined in the Core Systems Architecture Specification.
 */
typedef enum {
    STAGE_0_FIRMWARE,
    STAGE_1_KERNEL_INIT,
    STAGE_2_RUNTIME_BRINGUP,
    STAGE_3_SERVICE_ACTIVATION,
    STAGE_USERSPACE_SESSION
} SageOSBootStage;

void sageos_set_boot_stage(SageOSBootStage stage);
SageOSBootStage sageos_get_boot_stage(void);

#endif
