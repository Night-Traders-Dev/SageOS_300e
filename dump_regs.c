#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BASE 0x10001000
int main() {
    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    void *map = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, BASE);
    for (int i=0; i<0x40; i+=4) {
        printf("Reg 0x%03x: 0x%08x\n", i, *(volatile uint32_t *)(map + i));
    }
    return 0;
}
