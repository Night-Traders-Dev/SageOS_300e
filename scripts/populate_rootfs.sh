#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: FHS-compliant layout (bytecode only)

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"
SAGE_COMPILER="./sageos_build/sage_lang/core/sage"
COMPILER="python3 scripts/compile_to_sgvm.py"

echo "Populating FHS-compliant $ROOTFS directory..."

# 1. Clean and prepare structure
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"

# 2. Define standard FHS hierarchy
DIRS=(
    "bin"           # Essential command binaries
    "etc/sagelang"  # System configuration
    "lib/sagelang"  # Shared bytecode libraries
    "proc"          # Kernel/Process info
    "sys"           # Kernel/Device info
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

# 3. Define build mappings (Source Path : Target Directory)
MAPPINGS=(
    "$BUILD_DIR/core/sagelang:lib/sagelang"
    "$BUILD_DIR/etc/system/sagelang:etc/sagelang"
    "$BUILD_DIR/etc/commands:bin"
)

echo "  Compiling system files (.sage -> .sgvm)..."

for mapping in "${MAPPINGS[@]}"; do
    src_path="${mapping%%:*}"
    dst_path="$ROOTFS/${mapping##*:}"
    
    mkdir -p "$dst_path"
    
    # Process only .sage files
    find "$src_path" -maxdepth 1 -name "*.sage" | while read -r f; do
        [ -e "$f" ] || continue
        
        filename=$(basename "${f%.sage}")
        clean_sage="/tmp/$filename.clean.sage"
        bc_path="/tmp/$filename.bc"
        target_path="$dst_path/$filename.sgvm"
        
        echo "    Compiling: $(basename "$f") -> $target_path"
        
        # Strip comments
        sed 's|//.*||g' "$f" > "$clean_sage"
        
        # Compile to intermediate bytecode
        $SAGE_COMPILER --emit-vm "$clean_sage" -o "$bc_path"
        
        # Package into final SGVM format
        $COMPILER "$bc_path" -o "$target_path"
        
        # Cleanup temp artifacts
        rm -f "$clean_sage" "$bc_path"
    done
done

# 4. Copy specific binary assets to /lib
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

echo "Rootfs population complete (FHS-compliant, bytecode-only)!"
