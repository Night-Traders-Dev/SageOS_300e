#include "cros_ec.h"
#include "io.h"
#include <stdint.h>

#define EC_LPC_ADDR_HOST_DATA       0x62u
#define EC_LPC_ADDR_HOST_CMD        0x66u
#define EC_COMMAND_PROTOCOL_3       0xda

static int wait_ec_ready(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(EC_LPC_ADDR_HOST_CMD) & 0x01)) return 1;
    }
    return 0;
}

int cros_ec_command(uint16_t command, uint8_t version,
                    const void* out_data, int out_len,
                    void* in_data, int in_len) {
    
    struct ec_host_request {
        uint8_t struct_version;
        uint8_t checksum;
        uint16_t command;
        uint8_t command_version;
        uint8_t reserved;
        uint16_t data_len;
    } __attribute__((packed)) req;

    struct ec_host_response {
        uint8_t struct_version;
        uint8_t checksum;
        uint16_t result;
        uint16_t data_len;
        uint16_t reserved;
    } __attribute__((packed)) res;

    uint8_t checksum = 0;
    const uint8_t* p;

    req.struct_version = 3;
    req.checksum = 0;
    req.command = command;
    req.command_version = version;
    req.reserved = 0;
    req.data_len = (uint16_t)out_len;

    /* Calculate checksum */
    p = (const uint8_t*)&req;
    for (uint32_t i = 0; i < sizeof(req); i++) checksum += p[i];
    p = (const uint8_t*)out_data;
    for (int i = 0; i < out_len; i++) checksum += p[i];
    req.checksum = (uint8_t)(-checksum);

    if (!wait_ec_ready()) return -1;

    /* Send magic and header */
    outb(EC_LPC_ADDR_HOST_CMD, EC_COMMAND_PROTOCOL_3);
    p = (const uint8_t*)&req;
    for (uint32_t i = 0; i < sizeof(req); i++) outb(EC_LPC_ADDR_HOST_DATA, p[i]);
    
    /* Send data */
    p = (const uint8_t*)out_data;
    for (int i = 0; i < out_len; i++) outb(EC_LPC_ADDR_HOST_DATA, p[i]);

    /* Wait for result */
    if (!wait_ec_ready()) return -1;

    /* Read response header */
    uint8_t* rp = (uint8_t*)&res;
    for (uint32_t i = 0; i < sizeof(res); i++) rp[i] = inb(EC_LPC_ADDR_HOST_DATA);

    if (res.result != 0) return -(int)res.result;

    /* Read response data */
    int to_read = (res.data_len < in_len) ? res.data_len : in_len;
    uint8_t* dp = (uint8_t*)in_data;
    for (int i = 0; i < to_read; i++) dp[i] = inb(EC_LPC_ADDR_HOST_DATA);

    return to_read;
}

int cros_ec_get_lid_state(void) {
    struct ec_response_lid_probe resp;
    int ret = cros_ec_command(EC_CMD_LID_PROBE, 0, NULL, 0, &resp, sizeof(resp));
    if (ret < 0) return -1;
    return (resp.status & 1); /* 1=open, 0=closed */
}
