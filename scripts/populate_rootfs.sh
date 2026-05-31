#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: FHS-compliant layout

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"
COMPILER="python3 scripts/compile_to_sgvm.py"

echo "Populating FHS-compliant $ROOTFS directory..."

# 0. Clean and prepare structure
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"

# 1. Define FHS structure
DIRS=(
    "bin"           # Essential command binaries
    "etc"           # System configuration
    "lib"           # Shared libraries and bytecode
    "proc"          # Kernel/Process information
    "sys"           # Kernel/Device information
    "dev"           # Device nodes
    "tmp"           # Temporary files
    "usr/bin"       # User binaries
    "usr/lib"       # User libraries
    "var/log"       # System logs
    "mnt"           # Mount points
)

for dir in "${DIRS[@]}"; do
    mkdir -p "$ROOTFS/$dir"
done

# 2. Map system files to FHS locations
# Format: "source_dir:target_dir"
MAPPINGS=(
    "$BUILD_DIR/core/sagelang:lib/sagelang"
    "$BUILD_DIR/etc/system/sagelang:etc/sagelang"
    "$BUILD_DIR/etc/commands:bin"
)

echo "  Compiling and syncing system files..."
for mapping in "${MAPPINGS[@]}"; do
    src_path="${mapping%%:*}"
    dst_path="$ROOTFS/${mapping##*:}"
    
    mkdir -p "$dst_path"
    
    # Process only .sage files
    find "$src_path" -maxdepth 1 -name "*.sage" | while read -r f; do
        [ -e "$f" ] || continue
        filename=$(basename "${f%.sage}")
        target_path="$dst_path/$filename.sgvm"
        
        echo "    Compiling: $(basename "$f") -> $target_path"
        $COMPILER "$f" -o "$target_path"
    done
done

# 3. Copy binary assets to /lib
ASSETS=(
    "$BUILD_DIR/fs/vfs_bridge.bc:lib/vfs_bridge.bc"
    "$BUILD_DIR/shell/sage_shell.bc:lib/sage_shell.bc"
)

for asset in "${ASSETS[@]}"; do
    src="${asset%%:*}"
    dst="$ROOTFS/${asset##*:}"
    if [ -f "$src" ]; then
        echo "    Copying asset: $src -> $dst"
        cp "$src" "$dst"
    fi
done

# 4. Finalize environment
echo "Rootfs population complete (FHS-compliant)!"
