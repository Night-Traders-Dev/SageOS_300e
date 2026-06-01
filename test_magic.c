#include <stdio.h>
#include <stdint.h>
int main() {
    // Correct VirtIO magic is 0x74726976 (little endian "virt")
    // Let's check what my previous probe saw.
    // It saw 0x74726976.
    // Wait, 0x74726976 is "virt" on little-endian machine?
    // 'v' is 0x76, 'i' is 0x69, 'r' is 0x72, 't' is 0x74.
    // Little-endian: 0x76 0x69 0x72 0x74.
    // Wait, no. 'v' is 0x76. "virt" = 'v' 'i' 'r' 't' = 0x76 0x69 0x72 0x74.
    // Little-endian 32-bit: 0x74 0x72 0x69 0x76.
    // Yes, 0x74726976.
    uint32_t magic = 0x74726976;
    printf("Magic as hex: 0x%08x\n", magic);
    return 0;
}
