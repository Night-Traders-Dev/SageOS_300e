#!/usr/bin/env bash

# gen_virt_disk.sh - Create a virtual disk image for SageOS virt builds
#
# Layout:
# LBA 0-2047: MBR / Reserved
# FAT32: Configurable (default 4096MB)
# BTRFS: Configurable (default 512MB)
# SWAP: Configurable (default 512MB)

DISK_IMG=${DISK_IMG:-"virt.img"}
FAT_SIZE_MB=${1:-4096}
BTRFS_SIZE_MB=${2:-512}
SWAP_SIZE_MB=${3:-512}

echo "Generating $DISK_IMG..."
echo "Sizes - FAT32: ${FAT_SIZE_MB}MB, BTRFS: ${BTRFS_SIZE_MB}MB, SWAP: ${SWAP_SIZE_MB}MB"

# Calculate Offsets (in MB)
# 1MB for MBR/reserved
FAT_SEEK=1
BTRFS_SEEK=$((FAT_SEEK + FAT_SIZE_MB))
SWAP_SEEK=$((BTRFS_SEEK + BTRFS_SIZE_MB))
TOTAL_SIZE=$((SWAP_SEEK + SWAP_SIZE_MB))

# 1. Create components
dd if=/dev/zero of=fat.part bs=1M count=$FAT_SIZE_MB status=none
if command -v mkfs.fat >/dev/null; then
    mkfs.fat -F 32 fat.part >/dev/null
    echo "  [OK] Formatted FAT32 partition."
else
    echo "  [WARN] mkfs.fat not found, FAT32 partition will be empty."
fi

dd if=/dev/zero of=btrfs.part bs=1M count=$BTRFS_SIZE_MB status=none
if command -v mkfs.btrfs >/dev/null; then
    mkfs.btrfs -f -M btrfs.part >/dev/null
    echo "  [OK] Formatted BTRFS partition."
else
    echo "  [WARN] mkfs.btrfs not found, BTRFS partition will be empty."
fi

dd if=/dev/zero of=swap.part bs=1M count=$SWAP_SIZE_MB status=none
echo "  [INFO] Created SWAP placeholder partition."

# 2. Assemble the disk image
echo "  [INFO] Creating base image of ${TOTAL_SIZE}MB..."
dd if=/dev/zero of=$DISK_IMG bs=1M count=$TOTAL_SIZE status=none

# Write partitions at exact offsets
echo "  [INFO] Writing FAT32 at ${FAT_SEEK}MB..."
dd if=fat.part of=$DISK_IMG bs=1M seek=$FAT_SEEK conv=notrunc status=none

echo "  [INFO] Writing BTRFS at ${BTRFS_SEEK}MB..."
dd if=btrfs.part of=$DISK_IMG bs=1M seek=$BTRFS_SEEK conv=notrunc status=none

echo "  [INFO] Writing SWAP at ${SWAP_SEEK}MB..."
dd if=swap.part of=$DISK_IMG bs=1M seek=$SWAP_SEEK conv=notrunc status=none

# Cleanup
rm -f fat.part btrfs.part swap.part

# 3. Verification
SIG=$(dd if=$DISK_IMG bs=1 skip=1049086 count=2 2>/dev/null | xxd -p)
if [ "$SIG" = "55aa" ]; then
    echo "  [OK] Verified FAT32 boot signature at LBA 2048."
else
    echo "  [FAIL] Failed to verify FAT32 boot signature (got $SIG, expected 55aa)."
fi

echo "Successfully created $DISK_IMG ($(du -h $DISK_IMG | cut -f1))"
