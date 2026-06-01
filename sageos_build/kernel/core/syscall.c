#include "syscall_numbers.h"
#include "process.h"
#include "vfs.h"
#include "console.h"
#include "ipc.h"
#include "scheduler_ipc_ext.h"
#include <string.h>

#include <sys/stat.h>

#include "timer.h"
#include "sage_alloc.h"
#include "dmesg.h"
#include "telemetry.h"

/* Forward declaration of IPC dispatch router */
extern long ipc_syscall_dispatch(long num, long a1, long a2, long a3,
                                  long a4, long a5);

extern void power_reboot(void);
extern void power_shutdown(void);

static size_t safe_strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    if (!s) return 0;
    while (len < maxlen && s[len]) {
        len++;
    }
    return len;
}

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

/* Forward declarations of syscall implementations */
long sys_write(int fd, const void *buf, size_t count);
long sys_read(int fd, void *buf, size_t count);
long sys_open(const char *path, int flags, int mode);
long sys_close(int fd);
long sys_lseek(int fd, off_t offset, int whence);
long sys_fstat(int fd, struct stat *st);
long sys_brk(uintptr_t addr);
long sys_execve(const char *path, char *const argv[], char *const envp[]);
long sys_waitpid(int pid, int *status, int options);
long sys_unlink(const char *path);
long sys_getdents64(int fd, void *dirp, size_t count);
long sys_mkdir(const char *path, int mode);
long sys_getcwd(char *buf, size_t size);
long sys_chdir(const char *path);
long sys_dup2(int oldfd, int newfd);
long sys_gettimeofday(struct timeval *tv, void *tz);
long sys_nanosleep(const struct timespec *req, struct timespec *rem);
long sys_times(struct tms *buf);
void sys_exit(int code);
long sys_reboot(void);
long sys_shutdown(void);

long sys_vfork(void);

long syscall_dispatch(long num, long a1, long a2, long a3,
                      long a4, long a5) {
    task_t *t = current_task();
    trace_log(TRACE_SYSCALL_ENTER, (uint64_t)num, (uint64_t)a1);
    long ret = 0;
    switch (num) {
    case SYS_read:
        ret = sys_read((int)a1, (void *)a2, (size_t)a3);
        break;
    case SYS_write:
        ret = sys_write((int)a1, (const void *)a2, (size_t)a3);
        break;
    case SYS_open:
        ret = sys_open((const char *)a1, (int)a2, (int)a3);
        break;
    case SYS_close:
        ret = sys_close((int)a1);
        break;
    case SYS_fstat:
        ret = sys_fstat((int)a1, (struct stat *)a2);
        break;
    case SYS_lseek:
        ret = sys_lseek((int)a1, (off_t)a2, (int)a3);
        break;
    case SYS_brk:
        ret = sys_brk((uintptr_t)a1);
        break;
    case SYS_vfork:
        ret = sys_vfork();
        break;
    case SYS_execve:
        ret = sys_execve((const char *)a1, (char *const *)a2, (char *const *)a3);
        break;
    case SYS_waitpid:
        ret = sys_waitpid((int)a1, (int *)a2, (int)a3);
        break;
    case SYS_unlink:
        ret = sys_unlink((const char *)a1);
        break;
    case SYS_getdents64:
        ret = sys_getdents64((int)a1, (void *)a2, (size_t)a3);
        break;
    case SYS_getcwd:
        ret = sys_getcwd((char *)a1, (size_t)a2);
        break;
    case SYS_chdir:
        ret = sys_chdir((const char *)a1);
        break;
    case SYS_dup2:
        ret = sys_dup2((int)a1, (int)a2);
        break;
    case SYS_mkdir:
        ret = sys_mkdir((const char *)a1, (int)a2);
        break;
    case SYS_gettimeofday:
        ret = sys_gettimeofday((struct timeval *)a1, (void *)a2);
        break;
    case SYS_nanosleep:
        ret = sys_nanosleep((const struct timespec *)a1, (struct timespec *)a2);
        break;
    case SYS_times:
        ret = sys_times((struct tms *)a1);
        break;
    case SYS_reboot:
        ret = sys_reboot();
        break;
    case SYS_shutdown:
        ret = sys_shutdown();
        break;
    case SYS_exit:
        sys_exit((int)a1);
        ret = 0;
        break;
    case SYS_getpid:
        ret = (t) ? (long)t->id : 1;
        break;
    case SYS_kill:
        ret = -VFS_EINVAL;
        break;
    case SYS_isatty:
        ret = (a1 >= 0 && a1 <= 2) ? 1 : 0;
        break;

    /* IPC subsystem syscalls (200-244) */
    case SYS_ipc_endpoint_create:
    case SYS_ipc_channel_create:
    case SYS_ipc_port_create:
    case SYS_ipc_shm_create:
    case SYS_ipc_send:
    case SYS_ipc_recv:
    case SYS_ipc_call:
    case SYS_ipc_port_listen:
    case SYS_ipc_port_accept:
    case SYS_ipc_port_connect:
    case SYS_ipc_shm_map:
    case SYS_ipc_shm_unmap:
    case SYS_ipc_shm_grant:
    case SYS_ipc_ns_register:
    case SYS_ipc_ns_lookup:
    case SYS_ipc_ns_unbind:
    case SYS_ipc_object_destroy:
    case SYS_ipc_object_pause:
    case SYS_ipc_object_resume:
    case SYS_ipc_object_drain:
    case SYS_ipc_object_info:
    case SYS_ipc_object_stats:
    case SYS_ipc_cap_insert:
    case SYS_ipc_cap_narrow:
    case SYS_ipc_cap_revoke:
    case SYS_ipc_cap_dup:
        ret = ipc_syscall_dispatch(num, a1, a2, a3, a4, a5);
        break;

    default:
        ret = -VFS_EINVAL;
        break;
    }

    if (num != SYS_write || (a1 != 1 && a1 != 2)) {
        console_write("[SYSCALL] num=");
        console_u32((uint32_t)num);
        console_write(" a1=");
        console_u32((uint32_t)a1);
        console_write(" -> ");
        console_u32((uint32_t)ret);
        console_write("\n");
    }
    return ret;
}


/* --- Syscall Implementations --- */

bool task_has_vfs_cap(task_t *t, const char *path, uint32_t required_right) {
    if (!t) return false;
    if (!(t->permissions & PERM_VFS_CAP_ONLY)) return true;

    thread_ipc_ext_t *ext = thread_ipc_ext(t);
    for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
        ipc_capability_t *cap = &ext->cap_table.caps[i];
        if (ipc_cap_is_valid(cap) && (cap->object_type == IPC_OBJ_DIR || cap->object_type == IPC_OBJ_FILE)) {
            if (!(cap->rights & required_right)) continue;
            size_t cap_path_len = strlen(cap->path);
            if (strncmp(path, cap->path, cap_path_len) == 0) {
                if (cap->object_type == IPC_OBJ_DIR) {
                    if (path[cap_path_len] == '\0' || path[cap_path_len] == '/' || strcmp(cap->path, "/") == 0) {
                        return true;
                    }
                } else {
                    if (strcmp(path, cap->path) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

long sys_write(int fd, const void *buf, size_t count) {
    task_t *t = current_task();
    if (!buf) return -VFS_EINVAL;
    
    /* Handle stdout/stderr specially for early boot or if task is not fully setup */
    if (fd == 1 || fd == 2) {
        /* Direct to console */
        for (size_t i = 0; i < count; i++) {
            console_putc(((const char*)buf)[i]);
        }
        return count;
    }

    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    /* Regular file write via VFS */
    int ret = vfs_write(t->fd_table[fd].path, t->fd_table[fd].offset, buf, count);
    if (ret >= 0) {
        t->fd_table[fd].offset += ret;
    }
    return ret;
}

long sys_read(int fd, void *buf, size_t count) {
    task_t *t = current_task();
    if (!buf) return -VFS_EINVAL;
    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    /* Handle stdin */
    if (fd == 0) {
        /* Not implemented for now, return 0 (EOF) */
        return 0;
    }

    /* Regular file read via VFS */
    int ret = vfs_read(t->fd_table[fd].path, t->fd_table[fd].offset, buf, count);
    if (ret >= 0) {
        t->fd_table[fd].offset += ret;
    }
    return ret;
}

#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

/* Linux-compatible fcntl.h flags */
#define O_RDONLY  00000000
#define O_WRONLY  00000001
#define O_RDWR    00000002
#define O_CREAT   00000100
#define O_EXCL    00000200
#define O_NOCTTY  00000400
#define O_TRUNC   00001000
#define O_APPEND  00002000

long sys_open(const char *path, int flags, int mode) {
    task_t *t = current_task();
    if (!path || safe_strnlen(path, 1024) >= VFS_MAX_PATH) return -VFS_EINVAL;
    if (!t) return -VFS_EINVAL;

    uint32_t required = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) ? IPC_CAP_RIGHT_VFS_WRITE : IPC_CAP_RIGHT_VFS_READ;
    if (!task_has_vfs_cap(t, path, required)) {
        dmesg_log("SECURITY: Denied VFS open access (no capability)");
        return -VFS_EACCES;
    }

    /* Handle O_CREAT */
    VfsStat st;
    if (vfs_stat(path, &st) < 0) {
        if (flags & O_CREAT) {
            int ret = vfs_create(path);
            if (ret < 0) return (long)ret;
        } else {
            return -VFS_ENOENT;
        }
    } else {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return -VFS_EEXIST;
        }
        /* If O_TRUNC is set, we should truncate the file.
           VFS doesn't have truncate yet, but we can simulate by re-creating it if it is a file. */
        if ((flags & O_TRUNC) && (st.type == VFS_FILE)) {
            vfs_create(path);
        }
    }

    /* Find free FD */
    int fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (!t->fd_table[i].valid) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -VFS_ENOSPC;

    /* Initialize FD entry */
    t->fd_table[fd].valid = 1;
    strncpy(t->fd_table[fd].path, path, VFS_MAX_PATH);
    t->fd_table[fd].flags = flags;
    t->fd_table[fd].offset = (flags & O_APPEND) ? (off_t)st.size : 0;

    return fd;
}

long sys_close(int fd) {
    task_t *t = current_task();
    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    t->fd_table[fd].valid = 0;
    return 0;
}

long sys_lseek(int fd, off_t offset, int whence) {
    task_t *t = current_task();
    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    /* Simple lseek implementation */
    if (whence == 0) { /* SEEK_SET */
        t->fd_table[fd].offset = offset;
    } else if (whence == 1) { /* SEEK_CUR */
        t->fd_table[fd].offset += offset;
    } else if (whence == 2) { /* SEEK_END */
        VfsStat st;
        if (vfs_stat(t->fd_table[fd].path, &st) == 0) {
            t->fd_table[fd].offset = st.size + offset;
        } else {
            return -VFS_EIO;
        }
    } else {
        return -VFS_EINVAL;
    }

    return (long)t->fd_table[fd].offset;
}

long sys_fstat(int fd, struct stat *st) {
    task_t *t = current_task();
    if (!st) return -VFS_EINVAL;
    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    VfsStat vst;
    if (vfs_stat(t->fd_table[fd].path, &vst) < 0)
        return -VFS_EIO;

    memset(st, 0, sizeof(struct stat));
    st->st_size = (off_t)vst.size;
    
    if (vst.type == VFS_DIRECTORY) {
        st->st_mode = S_IFDIR | 0755;
    } else {
        st->st_mode = S_IFREG | 0644;
    }
    
    return 0;
}

long sys_unlink(const char *path) {
    task_t *t = current_task();
    if (!path || safe_strnlen(path, 1024) >= VFS_MAX_PATH) return -VFS_EINVAL;
    if (!task_has_vfs_cap(t, path, IPC_CAP_RIGHT_VFS_WRITE)) {
        dmesg_log("SECURITY: Denied VFS unlink access (no capability)");
        return -VFS_EACCES;
    }
    return (long)vfs_unlink(path);
}

struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

long sys_getdents64(int fd, void *dirp, size_t count) {
    task_t *t = current_task();
    if (!dirp) return -VFS_EINVAL;
    if (!t || fd < 0 || fd >= MAX_FD || !t->fd_table[fd].valid)
        return -VFS_EINVAL;

    VfsDirEntry entries[VFS_DIRENT_MAX];
    int n = vfs_readdir(t->fd_table[fd].path, entries, VFS_DIRENT_MAX);
    if (n < 0) return (long)n;

    uint8_t *out = (uint8_t *)dirp;
    size_t total_size = 0;
    
    for (int i = 0; i < n; i++) {
        size_t name_len = strlen(entries[i].name);
        size_t reclen = (8 + 8 + 2 + 1 + name_len + 1 + 7) & ~7; /* Align to 8 */
        
        if (total_size + reclen > count) break;

        struct linux_dirent64 *d = (struct linux_dirent64 *)(out + total_size);
        d->d_ino = i + 1;
        d->d_off = total_size + reclen;
        d->d_reclen = (unsigned short)reclen;
        d->d_type = (entries[i].type == VFS_DIRECTORY) ? 4 : 8; /* DT_DIR=4, DT_REG=8 */
        strcpy(d->d_name, entries[i].name);
        
        total_size += reclen;
    }
    
    return (long)total_size;
}

long sys_mkdir(const char *path, int mode) {
    task_t *t = current_task();
    if (!path || safe_strnlen(path, 1024) >= VFS_MAX_PATH) return -VFS_EINVAL;
    if (!task_has_vfs_cap(t, path, IPC_CAP_RIGHT_VFS_WRITE)) {
        dmesg_log("SECURITY: Denied VFS mkdir access (no capability)");
        return -VFS_EACCES;
    }
    (void)mode;
    return (long)vfs_mkdir(path);
}

long sys_getcwd(char *buf, size_t size) {
    task_t *t = current_task();
    if (!buf) return -VFS_EINVAL;
    if (!t) return -VFS_EINVAL;
    if (safe_strnlen(t->cwd, 256) >= size) return -VFS_ENOSPC;
    strcpy(buf, t->cwd);
    return (long)buf;
}

long sys_chdir(const char *path) {
    task_t *t = current_task();
    if (!path || safe_strnlen(path, 1024) >= VFS_MAX_PATH) return -VFS_EINVAL;
    if (!t) return -VFS_EINVAL;
    if (!task_has_vfs_cap(t, path, IPC_CAP_RIGHT_VFS_READ)) {
        dmesg_log("SECURITY: Denied VFS chdir access (no capability)");
        return -VFS_EACCES;
    }
    VfsStat st;
    if (vfs_stat(path, &st) < 0) return -VFS_ENOENT;
    if (st.type != VFS_DIRECTORY) return -VFS_ENOTDIR;
    strncpy(t->cwd, path, 255);
    return 0;
}

long sys_dup2(int oldfd, int newfd) {
    task_t *t = current_task();
    if (!t) return -VFS_EINVAL;
    if (oldfd < 0 || oldfd >= MAX_FD || !t->fd_table[oldfd].valid) return -VFS_EINVAL;
    if (newfd < 0 || newfd >= MAX_FD) return -VFS_EINVAL;
    
    t->fd_table[newfd] = t->fd_table[oldfd];
    return newfd;
}

long sys_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -VFS_EINVAL;
    tv->tv_sec = (long)timer_seconds();
    tv->tv_usec = (long)((timer_ticks() % 100) * 10000); /* Assuming 100 ticks per second */
    return 0;
}

long sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (!req) return -VFS_EINVAL;
    uint32_t ms = (uint32_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    timer_delay_ms(ms);
    return 0;
}

long sys_times(struct tms *buf) {
    if (!buf) return -VFS_EINVAL;
    memset(buf, 0, sizeof(struct tms));
    buf->tms_utime = (long)timer_ticks();
    return (long)timer_ticks();
}

extern void thread_switch(uint64_t *prev, uint64_t *next);
extern void thread_clone_and_switch(task_t *parent, task_t *child, uintptr_t offset);

uint64_t copy_kernel_stack(task_t *parent, task_t *child, uint64_t parent_sp, uintptr_t is_parent_offset) {
    uint64_t parent_stack_size = parent->stack_top - parent->stack_base;
    memcpy((void*)child->stack_base, (void*)parent->stack_base, parent_stack_size);
    
    uint64_t offset = parent_sp - parent->stack_base;
    uint64_t child_sp = child->stack_base + offset;

    child->rsp = child_sp;
    parent->rsp = parent_sp; /* Optional, as we might not resume here via thread_switch */

    volatile int *child_is_parent = (volatile int *)(child->stack_base + is_parent_offset);
    *child_is_parent = 0;

    return child_sp;
}

long sys_vfork(void) {
    task_t *parent = current_task();
    task_t *child = sched_create_thread("vfork_child", NULL, NULL, parent->priority);
    if (!child) return -VFS_ENOSPC;

    child->parent = parent;

    /* Copy file descriptors */
    for (int i = 0; i < MAX_FD; i++) {
        child->fd_table[i] = parent->fd_table[i];
    }
    child->heap_base = parent->heap_base;
    child->heap_limit = parent->heap_limit;
    child->heap_end = parent->heap_end;

    volatile int is_parent = 1;
    uintptr_t is_parent_offset = (uintptr_t)&is_parent - parent->stack_base;

    parent->state = THREAD_STATE_BLOCKED;
    
    /* We must update g_current_task because thread_clone_and_switch bypasses sched_schedule */
    extern thread_t *g_current_task;
    g_current_task = child;
    child->state = THREAD_STATE_RUNNING;

    thread_clone_and_switch(parent, child, is_parent_offset);

    /* When parent resumes, it must restore itself as current_task.
       Actually, sched_schedule will do this! Because sched_schedule switches back to parent.
       So when parent resumes here, g_current_task is already parent. */

    if (is_parent) {
        console_write("[sys_vfork] Returning to parent. child id=");
        console_u32((uint32_t)child->id);
        console_write("\n");
        return child->id;
    } else {
        console_write("[sys_vfork] Returning to child.\n");
        return 0;
    }
}

extern int sched_get_thread_info(uint32_t index, char *name, thread_state_t *state, uint32_t *cpu);
extern thread_t *sched_get_thread_by_id(uint32_t id);

long sys_waitpid(int pid, int *status, int options) {
    (void)options;
    task_t *parent = current_task();
    
    while (1) {
        task_t *child = sched_get_thread_by_id(pid);
        if (!child) return -VFS_EINVAL;

        if (child->state == THREAD_STATE_TERMINATED) {
            if (status) *status = child->exit_code;
            sched_ipc_cleanup_thread(child);
            child->state = THREAD_STATE_UNUSED; /* reap */
            return pid;
        }

        parent->state = THREAD_STATE_BLOCKED;
        sched_schedule();
    }
}

extern void sched_exit(void);

void sys_exit(int code) {
    task_t *t = current_task();
    t->exit_code = code;
    
    if (t->parent) {
        /* Restore parent's memory if we saved it */
        if (t->parent->saved_elf_data) {
            memcpy((void*)t->parent->elf_base, t->parent->saved_elf_data, t->parent->elf_size);
            sage_free(t->parent->saved_elf_data);
            t->parent->saved_elf_data = NULL;
        }
    }
    
    sched_exit();
}

long sys_reboot(void) {
    task_t *t = current_task();
    if (!t || !(t->permissions & PERM_SYS_REBOOT)) {
        dmesg_log("SECURITY: Denied reboot request from task (no PERM_SYS_REBOOT)");
        return -VFS_EACCES;
    }
    power_reboot();
    return 0;
}

long sys_shutdown(void) {
    task_t *t = current_task();
    if (!t || !(t->permissions & PERM_SYS_REBOOT)) {
        dmesg_log("SECURITY: Denied shutdown request from task (no PERM_SYS_REBOOT)");
        return -VFS_EACCES;
    }
    power_shutdown();
    return 0;
}
