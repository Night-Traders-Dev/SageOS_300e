#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: Define sources and target directories systematically.
# Compiles .sage to .sgvm bytecode.

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"
COMPILER="python3 scripts/compile_to_sgvm.py"

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

# MAPPINGS: source_path -> target_dir
# Syncing system files and commands
MAPPINGS=(
    "$BUILD_DIR/core/sagelang/*.sage:system/sagelang"
    "$BUILD_DIR/etc/system/sagelang/*.sage:etc/system/sagelang"
    "$BUILD_DIR/etc/commands/*.sage:etc/commands"
)

echo "  Compiling and syncing system files and commands..."
for mapping in "${MAPPINGS[@]}"; do
    src_glob="${mapping%%:*}"
    dst="${mapping##*:}"
    echo "    Processing $src_glob -> $dst"
    
    for f in $src_glob; do
        [ -e "$f" ] || continue
        filename=$(basename "${f%.sage}")
        target_path="$ROOTFS/$dst/$filename.sgvm"
        
        # Compile .sage to .sgvm
        $COMPILER "$f" -o "$target_path"
    done
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
for f in "$ROOTFS/etc/commands"/*.sgvm; do
    [ -e "$f" ] || continue
    name=$(basename "${f%.sgvm}")
    ln -sf "/etc/commands/$name.sgvm" "$ROOTFS/bin/$name"
done

echo "Rootfs population complete!"
