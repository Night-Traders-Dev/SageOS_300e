#!/usr/bin/env bash

# gen_virt_disk.sh - Create a virtual disk image for SageOS virt builds
#
# Layout:
# LBA 0-2047: MBR / Reserved
# LBA 2048: FAT32 (64MB)
# LBA 133120: BTRFS (128MB)
# LBA 395264: SWAP (125MB)

DISK_IMG="virt.img"
FAT_SIZE_MB=64
BTRFS_SIZE_MB=128
SWAP_SIZE_MB=125

echo "Generating $DISK_IMG..."

# 1. Create components
dd if=/dev/zero of=fat.part bs=1M count=$FAT_SIZE_MB status=none
if command -v mkfs.fat >/dev/null; then
    mkfs.fat -F 32 fat.part >/dev/null
    echo "  [OK] Formatted FAT32 partition."
else
    echo "  [WARN] mkfs.fat not found, FAT32 partition will be empty."
fi

dd if=/dev/zero of=btrfs.part bs=1M count=$BTRFS_SIZE_MB status=none
# mkfs.btrfs often requires root or special flags for small images, skipping for now
echo "  [INFO] Created BTRFS placeholder partition."

dd if=/dev/zero of=swap.part bs=1M count=$SWAP_SIZE_MB status=none
echo "  [INFO] Created SWAP placeholder partition."

# 2. Assemble the disk image
# Start with 1MB of zeros (LBA 0-2047)
dd if=/dev/zero of=$DISK_IMG bs=1M count=1 status=none

# Append partitions
dd if=fat.part >> $DISK_IMG status=none
dd if=btrfs.part >> $DISK_IMG status=none
dd if=swap.part >> $DISK_IMG status=none

# Cleanup
rm fat.part btrfs.part swap.part

echo "Successfully created $DISK_IMG ($(du -h $DISK_IMG | cut -f1))"
