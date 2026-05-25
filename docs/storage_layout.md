# SageOS Storage & Partition Layout

SageOS (v0.2.0+) uses a standardized GPT partition layout to support multiple filesystems and system features including EFI booting, high-performance root storage, and swap.

## Partition Table (GPT)

The following layout is used for both **Live** and **Installer** images:

| Partition | Name | Type | Filesystem | Size | Mount Point |
|---|---|---|---|---|---|
| 1 | EFI System | `EF00` | FAT32 | 64 MiB | `/fat32` |
| 2 | SageOS Root | `8300` | BTRFS | 128 MiB | `/btrfs` |
| 3 | SageOS Swap | `8200` | SWAP | 125 MiB | (Internal) |

### 1. EFI System Partition (FAT32)
- **Start LBA**: 2048
- **Role**: Standard UEFI boot partition. Contains `BOOTX64.EFI` (the loader) and `KERNEL.BIN`.
- **SageOS Use**: Mounted at `/fat32`. Used for persistent configuration (e.g., `WIFI.CFG`) and boot logging (`BOOTLOG.TXT`).

### 2. SageOS Root (BTRFS)
- **Start LBA**: 133120 (approx)
- **Role**: Primary system storage.
- **SageOS Use**: Mounted at `/btrfs`. Supports high-capacity storage and modern filesystem features.

### 3. SageOS Swap
- **Start LBA**: 395264 (approx)
- **Role**: Virtual memory expansion.
- **SageOS Use**: Managed by the kernel swap subsystem (`kernel/drivers/swap.c`). Provides 125MB of additional memory backing.

---

## Kernel Hardcoding

For robustness in early boot, the kernel uses standardized LBA offsets to locate these partitions:

- **FAT32**: Starts at LBA `2048`.
- **BTRFS**: Starts at LBA `2048 + (64MiB / 512)`.
- **SWAP**: Starts at LBA `2048 + (64MiB / 512) + (128MiB / 512)`.

These offsets are defined in:
- `sageos_build/kernel/fs/fat32.c`
- `sageos_build/kernel/fs/btrfs.c`
- `sageos_build/kernel/drivers/swap.c`

And enforced by the image generation logic in `lenovo_300e.sh`.

---

## Verification

You can verify the storage status from the SageShell:

```bash
# Check VFS mount points
ls /

# Check partition availability
ls /fat32
ls /btrfs

# Check swap status
swap
```
