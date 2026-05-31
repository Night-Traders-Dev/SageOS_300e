#!/usr/bin/env bash

# populate_rootfs.sh - Create and populate the SageOS rootfs directory
# Organization: FHS-compliant layout (bytecode only)

set -e

ROOTFS="rootfs"
BUILD_DIR="sageos_build/kernel"
SAGE_COMPILER="./sageos_build/sage_lang/core/sage"
COMPILER="python3 scripts/compile_to_sgvm.py"

echo "Populating FHS-compliant $ROOTFS directory..."

rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"
DIRS=("bin" "etc/sagelang" "lib/sagelang" "proc" "sys" "dev" "tmp" "usr/bin" "usr/lib" "var/log" "mnt")
for dir in "${DIRS[@]}"; do mkdir -p "$ROOTFS/$dir"; done

TMP_BC="/tmp/sage_compiled"
rm -rf "$TMP_BC"
mkdir -p "$TMP_BC"

echo "  Compiling system files (.sage -> .sgvm)..."

ALL_SAGE=$(find "$BUILD_DIR" -name "*.sage" | sort -u)

for f in $ALL_SAGE; do
    [ -e "$f" ] || continue
    filename=$(basename "${f%.sage}")
    clean_sage="/tmp/$filename.clean.sage"
    bc_path="/tmp/$filename.bc"
    sgvm_path="$TMP_BC/$filename.sgvm"
    
    echo "    Compiling: $(basename "$f")"
    sed 's|//.*||g' "$f" > "$clean_sage"
    
    # Try compiling; if it fails (e.g. AST fallback not supported), skip it
    if $SAGE_COMPILER --emit-vm "$clean_sage" -o "$bc_path" 2>/dev/null; then
        $COMPILER "$bc_path" -o "$sgvm_path"
        echo "      Success"
    else
        echo "      Skipped (VM compile unsupported)"
    fi
    
    rm -f "$clean_sage" "$bc_path"
done

# Syncing files
cp "$TMP_BC/runtime_manager.sgvm" "$ROOTFS/lib/sagelang/" 2>/dev/null || true
cp "$TMP_BC/core_drivers.sgvm" "$ROOTFS/lib/sagelang/" 2>/dev/null || true
cp "$TMP_BC/timer.sgvm" "$ROOTFS/lib/sagelang/" 2>/dev/null || true
cp "$TMP_BC/battery.sgvm" "$ROOTFS/etc/sagelang/" 2>/dev/null || true
cp "$TMP_BC/bootlog.sgvm" "$ROOTFS/etc/sagelang/" 2>/dev/null || true
cp "$TMP_BC/status.sgvm" "$ROOTFS/bin/status" 2>/dev/null || true

# Copy assets
ASSETS=("$BUILD_DIR/fs/vfs_bridge.bc:lib/vfs_bridge.bc" "$BUILD_DIR/shell/sage_shell.bc:lib/sage_shell.bc")
for asset in "${ASSETS[@]}"; do
    src="${asset%%:*}"; dst="$ROOTFS/${asset##*:}"
    if [ -f "$src" ]; then cp "$src" "$dst"; fi
done

rm -rf "$TMP_BC"
echo "Rootfs population complete!"
