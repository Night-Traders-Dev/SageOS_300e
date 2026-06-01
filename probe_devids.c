#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_STEP 0x1000
#define REG_MAGIC 0x000
#define REG_DEVICE_ID 0x008

int main() {
    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    for (int i = 0; i < 8; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_STEP);
        void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, base);
        if (map == MAP_FAILED) continue;
        uint32_t magic = *(volatile uint32_t *)(map + REG_MAGIC);
        uint32_t devid = *(volatile uint32_t *)(map + REG_DEVICE_ID);
        printf("Base: 0x%08llx, Magic: 0x%08x, DevID: %u\n", (unsigned long long)base, magic, devid);
        munmap(map, 0x1000);
    }
    return 0;
}
