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

echo "Installing native toolchain ($ARCH) into $DISK_IMG..."

# Ensure standard directories exist in the image root
mmd -i "$DISK_IMG@@1M" ::/bin 2>/dev/null || true
mmd -i "$DISK_IMG@@1M" ::/lib 2>/dev/null || true
mmd -i "$DISK_IMG@@1M" ::/include 2>/dev/null || true

# Copy bin, lib, include from the distribution
echo "Copying toolchain files from $NATIVE_DIST..."
# We use -D o to overwrite existing files
# Handle both native-build structure (usr/bin) and cross-tarball structure (bin/)
if [ -d "$NATIVE_DIST/usr/bin" ]; then
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/usr/bin" ::/
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/usr/lib" ::/
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/usr/include" ::/
else
    # For cross-tarballs, files are at the root of the prefix
    # We install them to /usr inside SageOS
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/bin" ::/usr/
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/lib" ::/usr/
    mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/include" ::/usr/
    # Also copy the target-specific directory which contains important headers/libs
    TARGET="${TAR_ARCH}-unknown-sageos"
    if [ -d "$NATIVE_DIST/$TARGET" ]; then
         mcopy -i "$DISK_IMG@@1M" -s -D o "$NATIVE_DIST/$TARGET" ::/usr/
    fi
fi

echo "Installation complete!"
