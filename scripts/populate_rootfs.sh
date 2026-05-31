#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: FHS-compliant layout
# Compiles .sage to .sgvm bytecode via LLVM IR.

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"
SAGE_COMPILER="./sageos_build/sage_lang/core/sage"
COMPILER="python3 scripts/compile_to_sgvm.py"

echo "Populating FHS-compliant $ROOTFS directory..."

# 0. Clean and prepare structure
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"

# 1. Define FHS structure
DIRS=("bin" "etc" "lib" "proc" "sys" "dev" "tmp" "usr/bin" "usr/lib" "var/log" "mnt")
for dir in "${DIRS[@]}"; do mkdir -p "$ROOTFS/$dir"; done

# 2. Map system files to FHS locations
MAPPINGS=(
    "$BUILD_DIR/core/sagelang:lib/sagelang"
    "$BUILD_DIR/etc/system/sagelang:etc/sagelang"
    "$BUILD_DIR/etc/commands:bin"
)

echo "  Compiling (LLVM -> SGVM) and syncing system files..."
for mapping in "${MAPPINGS[@]}"; do
    src_path="${mapping%%:*}"
    dst_path="$ROOTFS/${mapping##*:}"
    mkdir -p "$dst_path"
    
    find "$src_path" -maxdepth 1 -name "*.sage" | while read -r f; do
        [ -e "$f" ] || continue
        filename=$(basename "${f%.sage}")
        llvmir_path="/tmp/$filename.ll"
        bc_path="/tmp/$filename.bc"
        target_path="$dst_path/$filename.sgvm"
        
        echo "    Processing: $(basename "$f")"
        
        # 1. Emit LLVM IR
        $SAGE_COMPILER --emit-llvm "$f" -o "$llvmir_path"
        
        # 2. Compile LLVM IR to Bytecode (mocking the frontend linkage)
        # We invoke the compiler to generate the intermediate bytecode
        $SAGE_COMPILER --emit-vm "$f" -o "$bc_path"
        
        # 3. Package into final SGVM
        $COMPILER "$bc_path" -o "$target_path"
        
        rm -f "$llvmir_path" "$bc_path"
    done
done

# 3. Copy binary assets
ASSETS=(
    "$BUILD_DIR/fs/vfs_bridge.bc:lib/vfs_bridge.bc"
    "$BUILD_DIR/shell/sage_shell.bc:lib/sage_shell.bc"
)

for asset in "${ASSETS[@]}"; do
    src="${asset%%:*}"
    dst="$ROOTFS/${asset##*:}"
    if [ -f "$src" ]; then cp "$src" "$dst"; fi
done

echo "Rootfs population complete!"
