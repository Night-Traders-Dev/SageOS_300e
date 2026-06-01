#include <stdio.h>
#include <stdint.h>
int main() {
    uint64_t mmio_base = 0;
    uint64_t base = 0x0000000010008000;
    mmio_base = base;
    printf("mmio_base: 0x%016llx\n", (unsigned long long)mmio_base);
    return 0;
}
