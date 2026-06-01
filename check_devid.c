#include <stdio.h>
#include <stdint.h>
// Check if 0 is a valid block device ID for VirtIO
// According to VirtIO spec, block is 1
#define VIRTIO_ID_BLOCK 1
int main() {
    uint32_t devid = 0;
    if (devid == 1) printf("Block\n");
    else printf("Not Block (found: %u)\n", devid);
    return 0;
}
