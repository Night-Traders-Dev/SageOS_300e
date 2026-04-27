#ifndef SAGEOS_CROS_EC_H
#define SAGEOS_CROS_EC_H

#include <stdint.h>

#define EC_CMD_LID_PROBE 0x0058

struct ec_response_lid_probe {
    uint8_t status;
} __attribute__((packed));

int cros_ec_command(uint16_t command, uint8_t version,
                    const void* out_data, int out_len,
                    void* in_data, int in_len);

int cros_ec_get_lid_state(void);

#endif
