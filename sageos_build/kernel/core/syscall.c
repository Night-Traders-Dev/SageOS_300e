#include "syscall_numbers.h"
#include "process.h"
#include "vfs.h"
#include "console.h"
#include <string.h>

#include <sys/stat.h>

/* Forward declarations of syscall implementations */
long sys_write(int fd, const void *buf, size_t count);
long sys_read(int fd, void *buf, size_t count);
long sys_open(const char *path, int flags, int mode);
long sys_close(int fd);
long sys_lseek(int fd, off_t offset, int whence);
long sys_fstat(int fd, struct stat *st);
long sys_brk(uintptr_t addr);
long sys_execve(const char *path, char *const argv[], char *const envp[]);
void sys_exit(int code);

long syscall_dispatch(long num, long a1, long a2, long a3,
                      long a4, long a5) {
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
    case SYS_execve:
        return sys_execve((const char *)a1, (char *const *)a2, (char *const *)a3);
    case SYS_exit:
        sys_exit((int)a1);
        return 0; /* Unreachable */
    case SYS_getpid:
        return 1; /* Single process for now */
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

void sys_exit(int code) {
    /* For now, just terminate the current thread */
    /* In a full process model, this would clean up the task */
    console_write("\n[Process Exit] code ");
    console_u32((uint32_t)code);
    console_write("\n");
    
    /* We don't have a direct 'thread_exit' in scheduler.h, but we can sleep forever
     * or call a future sched_terminate_thread.
     */
    while(1) {
        /* Yield or wait */
    }
}
