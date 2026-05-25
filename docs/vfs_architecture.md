# SageOS Virtual Filesystem (VFS) Architecture

The Virtual Filesystem (VFS) is a key architectural subsystem in SageOS that abstracts storage backends (`ramfs`, `fat32`, `btrfs`), maps them to a unified path namespace, and exposes them directly to high-level userspace scripting in **SageLang** via a dynamic bytecode bridge.

---

## 1. Overview & Interface

The core kernel VFS is implemented in `kernel/fs/vfs.c`. It defines a generic `VfsBackend` structure representing filesystem-specific operations:

```c
typedef struct VfsBackend {
    const char *name;
    int (*stat)(const char *path, VfsStat *st);
    int (*readdir)(const char *path, VfsDirEntry *entries, int max_entries);
    int (*read)(const char *path, uint64_t offset, void *buffer, size_t size);
    int (*write)(const char *path, uint64_t offset, const void *buffer, size_t size);
    int (*mkdir)(const char *path);
    int (*create)(const char *path);
    int (*unlink)(const char *path);
    void *priv;
} VfsBackend;
```

Storage systems register their specific driver hooks under this structure:
- **RamFS** (`kernel/fs/ramfs.c`): In-memory volatile storage for system files, dynamically populated at boot.
- **FAT32** (`kernel/fs/fat32.c`): Legacy EFI System Partition (ESP) support. Includes a fully native write path and partition management.
- **BTRFS** (`kernel/fs/btrfs.c`): Modern Copy-On-Write (COW) fallback filesystem with full superblock reading capabilities.
- **SWAP** (`kernel/drivers/swap.c`): Kernel-level swap device management for virtual memory.

See **[Storage Layout](storage_layout.md)** for detailed partition information.

---

## 2. The Mount Table

Mounting is performed at boot time in `kmain()` (`kernel/core/kernel.c`) after the relevant backend drivers have successfully initialized. 

The mount points are stored in a simple global registry:
```c
#define MAX_MOUNTS 16
static VfsMount g_mounts[MAX_MOUNTS];
static int g_mount_count = 0;
```

### Handoff / Mount Layout (v0.2.0)
1. **`/`** — Mounted to `ramfs_get_backend()`. Houses system tools, package indices (`/etc/packages.json`), system configuration (`/etc/init.sage`), and runtime diagnostics.
2. **`/fat32`** — Mounted to `fat32_get_backend()`. Exposes the physical ESP partition of the boot USB or local storage. Used to load large binary drivers, blobs, and persist network/supplicant configurations (`WIFI.CFG`).
3. **`/btrfs`** — Mounted to `btrfs_get_backend()`. Exposes high-capacity BTRFS partitions for robust primary storage.

---

## 3. SageLang VFS Bridge

To decouple storage logic from kernel internals, SageOS implements a high-performance bytecode bridge (`vfs_bridge.sage`) compiled to bytecode and embedded into the kernel via `kernel/fs/vfs_bridge_bytecode.h`.

When a high-level command like `ls /fat32` or a SageLang script opens a file:
1. The SageLang virtual machine (**MetalVM**) invokes the embedded VFS bytecode.
2. The bytecode evaluates system security policies, handles path normalization, and calls native C VFS bindings through registered bindings:
   ```c
   vm_register_native(vm, "vfs_read_native", vfs_bridge_read);
   vm_register_native(vm, "vfs_write_native", vfs_bridge_write);
   vm_register_native(vm, "vfs_readdir_native", vfs_bridge_readdir);
   ```
3. Path prefixes are stripped at the VFS boundary so the target backend receives a relative filesystem path. For example, a request for `/fat32/ath10k/QCA6174/hw3.0/board-2.bin` resolves to a native FAT32 look-up of `ath10k/QCA6174/hw3.0/board-2.bin` on the ESP partition.

---

## 4. Case-Insensitive Resolution (v0.1.92 Fix)

Prior to **v0.1.92**, path-matching on the native FAT32 driver used strict case-sensitive comparisons. When booting from a standard FAT32 partition, short filenames (SFN) are automatically capitalized by partitioning utilities (`ATH10K`, `QCA6174`, `BOARD-2.BIN`). 

As a result:
- Early boot queries (which used UEFI `EfiFileProtocol->Open` under active boot services) matched the directories successfully due to UEFI's internal case-insensitivity.
- Post-boot drivers or user commands accessing the FAT32 driver natively failed to match mixed-case strings, causing driver failures (`Error: failed to read board-2.bin`).

### Optimization:
The comparison helper `streq` in `kernel/fs/fat32.c` has been redesigned to perform case-insensitive character comparisons, converting lowercase ASCII ranges `[a-z]` to uppercase dynamically during look-ups:

```c
static int streq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    char ca = *a;
    char cb = *b;
    if (ca >= 'a' && ca <= 'z') ca -= 32;
    if (cb >= 'a' && cb <= 'z') cb -= 32;
    return ca == cb;
}
```

This simple, robust optimization ensures that lowercase, uppercase, and mixed-case requests (such as hardcoded firmware asset paths) match the physical SFN records perfectly, preserving driver compatibility on standard media.

---

## 5. Subsystem Files

- **`kernel/fs/vfs.c`**: Mount-table registry, path normalization, core read/write dispatch.
- **`kernel/fs/fat32.c`**: FAT32 sectors mapping, cluster chains, file read/write, directory parsing, and `streq` case-insensitive matching.
- **`kernel/fs/vfs_bridge.sage`**: High-level SageLang-to-C VFS marshal.
- **`kernel/core/sagelang/core_drivers_bridge.c`**: MetalVM binding mappings for the VFS.
