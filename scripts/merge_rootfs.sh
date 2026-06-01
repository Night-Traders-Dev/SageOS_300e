#!/usr/bin/env bash

# merge_rootfs.sh - Merge the rootfs directory into the virtual disk image

set -e

DISK_IMG=${DISK_IMG:-"virt.img"}
ROOTFS="rootfs"

if [ ! -f "$DISK_IMG" ]; then
    echo "Error: $DISK_IMG not found."
    exit 1
fi

if [ ! -d "$ROOTFS" ]; then
    echo "Error: $ROOTFS directory not found."
    exit 1
fi

echo "Merging $ROOTFS into $DISK_IMG..."

# Create the directory structure on the image first
# Use find -mindepth 1 to skip the root directory itself
find "$ROOTFS" -mindepth 1 -type d | while read -r dir; do
    rel_dir=${dir#"$ROOTFS/"}
    if [ -n "$rel_dir" ]; then
        echo "  MKDIR ::$rel_dir"
        mmd -D s -i "$DISK_IMG@@1M" "::$rel_dir" 2>/dev/null || true
    fi
done

# Copy all files explicitly
find "$ROOTFS" -type f | while read -r file; do
    rel_file=${file#"$ROOTFS/"}
    echo "  COPY $rel_file"
    mcopy -o -i "$DISK_IMG@@1M" "$file" "::$rel_file"
done

echo "Merge complete!"
