#include "boot_stages.h"
#include "console.h"
#include "dmesg.h"

static SageOSBootStage g_current_stage = STAGE_0_FIRMWARE;

const char* stage_names[] = {
    "STAGE 0: Firmware",
    "STAGE 1: Early Kernel Initialization",
    "STAGE 2: SGVM Runtime Bring-up",
    "STAGE 3: System Service Activation",
    "Userspace Session"
};

void sageos_set_boot_stage(SageOSBootStage stage) {
    g_current_stage = stage;
    if (stage <= STAGE_USERSPACE_SESSION) {
        dmesg_log("BOOT: Transitioning to %s", stage_names[stage]);
    }
}

SageOSBootStage sageos_get_boot_stage(void) {
    return g_current_stage;
}
