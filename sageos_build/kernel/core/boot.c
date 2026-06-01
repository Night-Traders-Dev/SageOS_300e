#include "boot_stages.h"
#include "console.h"
#include "dmesg.h"

static SageOSBootStage g_current_stage = STAGE_0_FIRMWARE;

const char* stage_names[] = {
    "STAGE 0: Firmware",
    "STAGE 1: Early Memory Management",
    "STAGE 2: IRQ & System Init",
    "STAGE 3: Device Discovery & IPC",
    "STAGE 4: Storage & VFS Mounting",
    "STAGE 5: Runtime Bring-up",
    "STAGE 6: Service Activation",
    "STAGE 7: Userspace Session",
    "System Halt"
};

void sageos_set_boot_stage(SageOSBootStage stage) {
    g_current_stage = stage;
    if (stage <= STAGE_HALT) {
        dmesg_printf("BOOT: Transitioning to %s", stage_names[stage]);
    }
}

SageOSBootStage sageos_get_boot_stage(void) {
    return g_current_stage;
}
