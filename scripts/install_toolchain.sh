#!/usr/bin/env bash

# install_toolchain.sh - Install native toolchain into SageOS disk image

set -e

ARCH=${1:-"x86_64"}
DISK_IMG="virt.img"
NATIVE_DIST=${2:-"/home/kraken/sageos-native-dist"}
TOOLCHAIN_TAG="v0.4.0-toolchain"

# Map script arch to tarball arch
TAR_ARCH="$ARCH"
if [[ "$ARCH" == "x64" ]]; then TAR_ARCH="x86_64"; fi
if [[ "$ARCH" == "arm64" ]]; then TAR_ARCH="aarch64"; fi

TARBALL="sageos-toolchain-${TAR_ARCH}.tar.gz"
DOWNLOAD_DIR="/tmp/sageos-toolchain-download"

if [ ! -d "$NATIVE_DIST" ]; then
    echo "Local native distribution not found at $NATIVE_DIST."
    echo "Attempting to download prebuilt toolchain ($TAR_ARCH) from GitHub..."
    
    mkdir -p "$DOWNLOAD_DIR"
    cd "$DOWNLOAD_DIR"
    
    if [ ! -f "$TARBALL" ]; then
        if command -v gh >/dev/null && [ -n "$GH_TOKEN" ]; then
            gh release download "$TOOLCHAIN_TAG" -p "$TARBALL" --repo "Night-Traders-Dev/SageOS"
        else
            URL="https://github.com/Night-Traders-Dev/SageOS/releases/download/${TOOLCHAIN_TAG}/${TARBALL}"
            echo "Downloading via curl: $URL"
            curl -L -O "$URL"
        fi
    fi
    
    echo "Extracting $TARBALL..."
    # The tarballs contain /home/kraken/sageos-toolchain-${TAR_ARCH}/
    # We want to extract it and use its usr/ equivalent (the prefix was /usr in the native build)
    # Wait, the native build used PREFIX=/usr but was installed to a DESTDIR.
    # The tarballs created earlier were from the CROSS toolchain build.
    # Re-reading: The user wants to use the prebuilt toolchain tarball FOR INSTALLING IN SAGEOS.
    
    tar -xzf "$TARBALL"
    # The cross toolchain tarball has: bin/, lib/, include/, sysroot/ etc.
    # We need to point NATIVE_DIST to the extracted directory.
    NATIVE_DIST="${DOWNLOAD_DIR}/sageos-toolchain-${TAR_ARCH}"
    cd - > /dev/null
fi

if [ ! -f "$DISK_IMG" ]; then
    echo "Error: $DISK_IMG not found. Run scripts/gen_virt_disk.sh first."
    exit 1
fi

# Check if already installed to avoid costly re-copy
if mdir -i "$DISK_IMG@@1M" ::/bin/gcc >/dev/null 2>&1 || mdir -i "$DISK_IMG@@1M" ::/bin/as >/dev/null 2>&1; then
    echo "Toolchain already detected in $DISK_IMG. Skipping installation."
    exit 0
fi

echo "Installing native toolchain ($ARCH) into $DISK_IMG..."
echo "WARNING: This is a large operation (1.2GB+) and may take several minutes depending on your disk speed."

# Ensure standard directories exist in the image root (use -D s to avoid interactive prompts on name clashes)
mmd -D s -i "$DISK_IMG@@1M" ::/bin 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/lib 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/include 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/libexec 2>/dev/null || true
mmd -D s -i "$DISK_IMG@@1M" ::/share 2>/dev/null || true

# Stage files to a single directory to minimize mcopy calls
echo "Staging files for transfer..."
STAGE_DIR="/tmp/sageos-toolchain-stage"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

if [ -d "$NATIVE_DIST/usr/bin" ]; then
    for dir in bin lib include libexec share; do
        if [ -d "$NATIVE_DIST/usr/$dir" ]; then
            cp -r "$NATIVE_DIST/usr/$dir" "$STAGE_DIR/"
        fi
    done
else
    for dir in bin lib include libexec share; do
        if [ -d "$NATIVE_DIST/$dir" ]; then
            cp -r "$NATIVE_DIST/$dir" "$STAGE_DIR/"
        fi
    done
    TARGET="${TAR_ARCH}-unknown-sageos"
    if [ -d "$NATIVE_DIST/$TARGET" ]; then
         cp -r "$NATIVE_DIST/$TARGET" "$STAGE_DIR/"
    fi
fi

echo "Copying files to disk image (this is the slow part)..."
# Using -m to preserve modification times, might be faster or more reliable
mcopy -o -v -i "$DISK_IMG@@1M" -s "$STAGE_DIR"/* ::/

echo "Installation complete!"
rm -rf "$STAGE_DIR"
