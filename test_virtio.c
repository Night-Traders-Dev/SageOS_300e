#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define VIRTIO_MMIO_BASE 0x10001000
#define MMIO_SIZE 0x10000

int main() {
    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Map a page to check if it's readable
    void *map = mmap(NULL, MMIO_SIZE, PROT_READ, MAP_SHARED, fd, VIRTIO_MMIO_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    uint32_t magic = *(volatile uint32_t *)map;
    printf("Magic: 0x%08x\n", magic);
    
    return 0;
}
