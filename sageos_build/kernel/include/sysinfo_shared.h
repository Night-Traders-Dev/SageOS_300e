#ifndef SAGEOS_SYSINFO_SHARED_H
#define SAGEOS_SYSINFO_SHARED_H
#include <stdint.h>
uint64_t ram_total_bytes(void);
uint64_t ram_used_bytes(void);
uint64_t pmm_count_used_frames(void);
#endif
