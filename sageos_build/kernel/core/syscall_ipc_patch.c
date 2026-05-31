/* ============================================================================
 * SageOS Syscall Dispatch — IPC Integration Patch
 * 
 * This file shows the minimal changes required in core/syscall.c to wire
 * the IPC subsystem into the existing syscall dispatch table.
 * 
 * Apply by inserting the IPC cases into syscall_dispatch() and adding
 * the forward declaration of ipc_syscall_dispatch().
 * ============================================================================ */

/* --- Add to top of syscall.c, after existing includes --- */
#include "ipc.h"

/* Forward declaration of IPC dispatch router */
extern long ipc_syscall_dispatch(long num, long a1, long a2, long a3,
                                  long a4, long a5);

/* --- Add inside syscall_dispatch() switch, before the default case --- */

    /* IPC subsystem syscalls (200-244) */
    case SYS_ipc_endpoint_create:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_channel_create:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_port_create:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_shm_create:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_send:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_recv:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_call:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_port_listen:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_port_accept:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_port_connect:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_shm_map:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_shm_unmap:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_shm_grant:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_ns_register:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_ns_lookup:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_ns_unbind:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_destroy:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_pause:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_resume:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_drain:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_info:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_object_stats:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_cap_insert:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_cap_narrow:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_cap_revoke:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
    case SYS_ipc_cap_dup:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;
