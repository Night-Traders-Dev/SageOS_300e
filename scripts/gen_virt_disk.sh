#!/usr/bin/env bash

# gen_virt_disk.sh - Create a virtual disk image for SageOS virt builds
#
# Layout:
# LBA 0-2047: MBR / Reserved
# LBA 2048: FAT32 (512MB)
# LBA 1050624: BTRFS (512MB)
# LBA 2099200: SWAP (125MB)

DISK_IMG="virt.img"
FAT_SIZE_MB=512
BTRFS_SIZE_MB=512
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
if command -v mkfs.btrfs >/dev/null; then
    mkfs.btrfs -f -M btrfs.part >/dev/null
    echo "  [OK] Formatted BTRFS partition."
else
    echo "  [WARN] mkfs.btrfs not found, BTRFS partition will be empty."
fi

dd if=/dev/zero of=swap.part bs=1M count=$SWAP_SIZE_MB status=none
echo "  [INFO] Created SWAP placeholder partition."

# 2. Assemble the disk image
# Create base image of 1150MB (total)
dd if=/dev/zero of=$DISK_IMG bs=1M count=1150 status=none

# Write partitions at exact offsets
# LBA 2048 = 1048576 bytes (1MB)
dd if=fat.part of=$DISK_IMG bs=1M seek=1 conv=notrunc status=none
# LBA 1050624 = 537919488 bytes (513MB)
dd if=btrfs.part of=$DISK_IMG bs=1M seek=513 conv=notrunc status=none
# LBA 2099200 = 1074790400 bytes (1025MB)
dd if=swap.part of=$DISK_IMG bs=1M seek=1025 conv=notrunc status=none

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
