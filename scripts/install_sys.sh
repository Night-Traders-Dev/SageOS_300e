#!/usr/bin/env bash

# install_sys.sh - Install core SageOS system scripts into the virtual disk image

set -e

DISK_IMG=${DISK_IMG:-"virt.img"}
SYS_SRC_DIR="sageos_build/kernel/core/sagelang"
SYS_DEST_DIR="::/system/sagelang"

if [ ! -f "$DISK_IMG" ]; then
    echo "Error: $DISK_IMG not found."
    exit 1
fi

echo "Installing core system scripts into $DISK_IMG..."

# Ensure directories exist
mmd -D s -i "$DISK_IMG@@1M" ::/system 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/system/sagelang 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/etc 2>/dev/null || true

# Copy all .sage files from kernel core sagelang
for f in "$SYS_SRC_DIR"/*.sage; do
    if [ -f "$f" ]; then
        echo "  Installing $(basename "$f")..."
        mcopy -o -i "$DISK_IMG@@1M" "$f" "$SYS_DEST_DIR/"
    fi
done

# Also copy them to /etc for legacy compatibility if needed
# mcopy -o -i "$DISK_IMG@@1M" "$SYS_SRC_DIR/runtime_manager.sage" ::/etc/init.sage

echo "System scripts installation complete!"
