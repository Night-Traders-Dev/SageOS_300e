#include "process.h"
#include "vfs.h"
#include "phys_alloc.h"
#include "vmm.h"
#include "dmesg.h"
#include <string.h>

#define MAX_RAM (128 * 1024 * 1024)
#define MAX_FRAMES (MAX_RAM / PAGE_SIZE)
#define BITMAP_SIZE (MAX_FRAMES / 8)

static uint8_t pmm_bitmap[BITMAP_SIZE];
static uint64_t pmm_total_frames = 0;
static uint64_t pmm_used_frames = 0;

/* 
 * sys_brk: Change the data segment size (heap)
 */
long sys_brk(uintptr_t addr) {
    task_t *t = current_task();
    if (t == NULL) return -1;

    /* If addr is 0, return the current end of the heap */
    if (addr == 0)
        return (long)t->heap_end;

    /* Validate bounds */
    if (addr < t->heap_base || addr > t->heap_limit)
        return VFS_EINVAL; /* Using VFS_EINVAL as a fallback for -ENOMEM for now */

    t->heap_end = addr;
    return (long)addr;
}

static void pmm_set_frame(uint64_t frame) {
    pmm_bitmap[frame / 8] |= (1 << (frame % 8));
}

static void pmm_unset_frame(uint64_t frame) {
    pmm_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static int pmm_is_frame_set(uint64_t frame) {
    return (pmm_bitmap[frame / 8] & (1 << (frame % 8)));
}

void phys_init(SageOSBootInfo *info) {
    memset(pmm_bitmap, 0xFF, BITMAP_SIZE); // Mark all as reserved initially
    
    pmm_total_frames = info->memory_total / PAGE_SIZE;
    if (pmm_total_frames > MAX_FRAMES) pmm_total_frames = MAX_FRAMES;

    // For now, let's just mark the usable memory as free.
    // In a real UEFI system, we'd parse the memory map.
    // Since we are in a "virt" environment, we'll assume memory is contiguous for now
    // but we reserve the first 1MB and the kernel area.
    
    uint64_t usable_start = 0x100000; // 1MB
    uint64_t usable_end = info->memory_total;
    
    for (uint64_t addr = usable_start; addr < usable_end; addr += PAGE_SIZE) {
        uint64_t frame = addr / PAGE_SIZE;
        if (frame < pmm_total_frames) {
            pmm_unset_frame(frame);
        }
    }

    // Reserve kernel area
    uint64_t kernel_start = info->kernel_base;
    uint64_t kernel_end = info->kernel_base + info->kernel_size;
    for (uint64_t addr = kernel_start; addr < kernel_end; addr += PAGE_SIZE) {
        pmm_set_frame(addr / PAGE_SIZE);
    }

    dmesg_log("PMM: Bitmap-backed allocator initialized. Total: %d MB, Usable: %d MB", 
              (int)(info->memory_total / 1024 / 1024), 
              (int)(info->memory_usable / 1024 / 1024));
}

void* phys_alloc(void) {
    for (uint64_t i = 0; i < pmm_total_frames; i++) {
        if (!pmm_is_frame_set(i)) {
            pmm_set_frame(i);
            pmm_used_frames++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void phys_free(void *addr) {
    uint64_t frame = (uintptr_t)addr / PAGE_SIZE;
    if (frame < pmm_total_frames) {
        pmm_unset_frame(frame);
        pmm_used_frames--;
    }
}

void vmm_init(void) {
    dmesg_log("VMM: Identity paging established (Stub)");
}

void vmm_map(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    (void)vaddr; (void)paddr; (void)flags;
}
