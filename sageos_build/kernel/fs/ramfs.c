#include <stddef.h>
#include "console.h"
#include "ramfs.h"

typedef struct {
    const char *path;
    const char *content;
} RamFile;

static const RamFile files[] = {
    {"/etc/motd", "Welcome to SageOS modular v0.1.1.\nType help for commands.\n"},
    {"/etc/version", "SageOS 0.1.1 modular kernel\n"},
    {"/bin/sh", "Kernel-resident shell\n"},
    {"/dev/fb0", "UEFI GOP framebuffer\n"},
    {"/proc/input", "native-i8042-ps2\n"},
};

static int eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

const char *ramfs_find(const char *path) {
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (eq(path, files[i].path)) return files[i].content;
    }

    return 0;
}

void ramfs_ls(void) {
    console_write("\n/");
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        console_write("\n");
        console_write(files[i].path);
    }
}
