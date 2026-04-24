#include "vfs.h"
#include "console.h"
#include "ramfs.h"
#include <stddef.h>

void vfs_init(void) {
    /* 
     * In a real kernel, we would mount different filesystems here.
     * For now, we bridge the existing ramfs to the VFS.
     */
}

VfsNode *vfs_find(const char *path) {
    /* Placeholder: real path parsing would go here */
    (void)path;
    return NULL;
}

void vfs_ls(const char *path) {
    if (path[0] == '/' && path[1] == 0) {
        ramfs_ls();
    }
}
