#ifndef SAGE_VM_HAL_H
#define SAGE_VM_HAL_H

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// SageLang VM Hardware Abstraction Layer (HAL)
// ============================================================================
// Defines required primitives for the VM to run in various environments
// (bare-metal kernel vs. standard host library).

#ifdef __cplusplus
extern "C" {
#endif

// Memory primitives
void* vm_memset(void* s, int c, size_t n);
void* vm_memcpy(void* dest, const void* src, size_t n);
size_t vm_strlen(const char* s);

// I/O primitives
void vm_console_write(const char* s);
void vm_console_u32(uint32_t v);

// Memory Allocation/Pool primitives
// In bare-metal, these map to static arenas. In host, to malloc/GC.
void* vm_alloc(size_t size);
void  vm_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // SAGE_VM_HAL_H
