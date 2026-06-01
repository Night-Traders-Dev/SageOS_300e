#include <stdio.h>
#include <stdint.h>
int main() {
    uint32_t magic = 0x76697274; // 'v' 'i' 'r' 't' in big endian?
    printf("Magic: 0x%08x\n", magic);
    return 0;
}
