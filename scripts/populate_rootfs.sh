#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: Define sources and target directories systematically.

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"

echo "Populating $ROOTFS directory..."

# 0. Clean and prepare structure
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"

# Define directories to create
DIRS=(
    "bin"
    "etc/commands"
    "etc/system/sagelang"
    "lib"
    "system/sagelang"
    "usr/bin"
    "usr/lib"
    "dev"
    "proc"
    "tmp"
    "mnt/fat32"
    "mnt/btrfs"
)

for dir in "${DIRS[@]}"; do
    mkdir -p "$ROOTFS/$dir"
done

# Define mappings: source_path -> target_dir
# Using an array of strings formatted as "source:target"
MAPPINGS=(
    "$BUILD_DIR/core/sagelang/*.sage:system/sagelang"
    "$BUILD_DIR/etc/system/sagelang/*.sage:etc/system/sagelang"
    "$BUILD_DIR/etc/commands/*.sage:etc/commands"
)

echo "  Populating system files and commands..."
for mapping in "${MAPPINGS[@]}"; do
    src="${mapping%%:*}"
    dst="${mapping##*:}"
    echo "    Syncing $src -> $dst"
    cp $src "$ROOTFS/$dst/"
done

# 4. Copy specific binary/bytecode assets
ASSETS=(
    "$BUILD_DIR/fs/vfs_bridge.bc:lib/vfs_bridge.bc"
    "$BUILD_DIR/shell/sage_shell.bc:lib/sage_shell.bc"
)

for asset in "${ASSETS[@]}"; do
    src="${asset%%:*}"
    dst="${asset##*:}"
    if [ -f "$src" ]; then
        echo "    Copying asset: $src -> $dst"
        cp "$src" "$ROOTFS/$dst"
    fi
done

# 5. Populate /bin with command aliases
echo "  Generating binary command aliases..."
for f in "$ROOTFS/etc/commands"/*.sage; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    ln -sf "/etc/commands/$name" "$ROOTFS/bin/$name"
done

echo "Rootfs population complete!"
