#ifndef SAGEOS_VFS_H
#define SAGEOS_VFS_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VFS_FILE,
    VFS_DIRECTORY
} VfsNodeType;

typedef struct VfsNode {
    char name[64];
    VfsNodeType type;
    uint64_t size;
    struct VfsNode *next;
    
    int (*read)(struct VfsNode *node, uint64_t offset, size_t size, void *buffer);
    void *priv;
} VfsNode;

void vfs_init(void);
VfsNode *vfs_find(const char *path);
void vfs_ls(const char *path);

#endif
