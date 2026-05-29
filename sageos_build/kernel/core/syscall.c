#include "syscall_numbers.h"
#include "process.h"
#include "vfs.h"
#include "console.h"
#include <string.h>

#include <sys/stat.h>

#include "timer.h"
#include "sage_alloc.h"

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

long sys_vfork(void);

long syscall_dispatch(long num, long a1, long a2, long a3,
                      long a4, long a5) {
    task_t *t = current_task();
    switch (num) {
    case SYS_read:
        return sys_read((int)a1, (void *)a2, (size_t)a3);
    case SYS_write:
        return sys_write((int)a1, (const void *)a2, (size_t)a3);
    case SYS_open:
        return sys_open((const char *)a1, (int)a2, (int)a3);
    case SYS_close:
        return sys_close((int)a1);
    case SYS_fstat:
        return sys_fstat((int)a1, (struct stat *)a2);
    case SYS_lseek:
        return sys_lseek((int)a1, (off_t)a2, (int)a3);
    case SYS_brk:
        return sys_brk((uintptr_t)a1);
    case SYS_vfork:
        return sys_vfork();
    case SYS_execve:
        return sys_execve((const char *)a1, (char *const *)a2, (char *const *)a3);
    case SYS_waitpid:
        return sys_waitpid((int)a1, (int *)a2, (int)a3);
    case SYS_unlink:
        return sys_unlink((const char *)a1);
    case SYS_getdents64:
        return sys_getdents64((int)a1, (void *)a2, (size_t)a3);
    case SYS_getcwd:
        return sys_getcwd((char *)a1, (size_t)a2);
    case SYS_chdir:
        return sys_chdir((const char *)a1);
    case SYS_dup2:
        return sys_dup2((int)a1, (int)a2);
    case SYS_mkdir:
        return sys_mkdir((const char *)a1, (int)a2);
    case SYS_gettimeofday:
        return sys_gettimeofday((struct timeval *)a1, (void *)a2);
    case SYS_nanosleep:
        return sys_nanosleep((const struct timespec *)a1, (struct timespec *)a2);
    case SYS_times:
        return sys_times((struct tms *)a1);
    case SYS_exit:
        sys_exit((int)a1);
        return 0; /* Unreachable */
    case SYS_getpid:
        return (t) ? (long)t->id : 1;
    case SYS_kill:
        return -VFS_EINVAL; /* Not implemented */
    case SYS_isatty:
        return (a1 >= 0 && a1 <= 2) ? 1 : 0;
    default:
        return -VFS_EINVAL;
    }
}

/* --- Syscall Implementations --- */

long sys_write(int fd, const void *buf, size_t count) {
    task_t *t = current_task();
    
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

long sys_open(const char *path, int flags, int mode) {
    task_t *t = current_task();
    if (!t) return -VFS_EINVAL;

    /* Find free FD */
    int fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (!t->fd_table[i].valid) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -VFS_ENOSPC;

    /* Check if file exists (VFS currently doesn't have an 'open' but we can check with stat) */
    VfsStat st;
    if (vfs_stat(path, &st) < 0) {
        /* If VFS_O_CREATE is supported in the future, we would create it here */
        return -VFS_ENOENT;
    }

    /* Initialize FD entry */
    t->fd_table[fd].valid = 1;
    strncpy(t->fd_table[fd].path, path, VFS_MAX_PATH);
    t->fd_table[fd].flags = flags;
    t->fd_table[fd].offset = 0;

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
    (void)mode;
    return (long)vfs_mkdir(path);
}

long sys_getcwd(char *buf, size_t size) {
    task_t *t = current_task();
    if (!t) return -VFS_EINVAL;
    if (strlen(t->cwd) >= size) return -VFS_ENOSPC;
    strcpy(buf, t->cwd);
    return (long)buf;
}

long sys_chdir(const char *path) {
    task_t *t = current_task();
    if (!t) return -VFS_EINVAL;
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
    if (tv) {
        tv->tv_sec = (long)timer_seconds();
        tv->tv_usec = (long)((timer_ticks() % 100) * 10000); /* Assuming 100 ticks per second */
    }
    return 0;
}

long sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req) {
        uint32_t ms = (uint32_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
        timer_delay_ms(ms);
    }
    return 0;
}

long sys_times(struct tms *buf) {
    if (buf) {
        memset(buf, 0, sizeof(struct tms));
        buf->tms_utime = (long)timer_ticks();
    }
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
