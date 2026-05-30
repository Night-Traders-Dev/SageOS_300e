#!/usr/bin/env bash

set -e

ARCHS=("$@")
if [ ${#ARCHS[@]} -eq 0 ]; then
    ARCHS=("x86_64" "aarch64" "riscv64")
fi

ROOT_DIR=$(pwd)
TOOLCHAIN_SCRIPT="${ROOT_DIR}/toolchain/build_toolchain.sh"
BUILD_DIR="${ROOT_DIR}/toolchain_build"

for ARCH in "${ARCHS[@]}"; do
    echo "========================================"
    echo "Processing $ARCH..."
    echo "========================================"
    
    PREFIX="/home/kraken/sageos-toolchain-${ARCH}"
    TARBALL="sageos-toolchain-${ARCH}.tar.gz"
    
    # Clean build dir to avoid poisoned state
    rm -rf "${BUILD_DIR}/build-binutils-${ARCH}" "${BUILD_DIR}/build-gcc-${ARCH}" "${BUILD_DIR}/build-newlib-${ARCH}"
    
    # Run the build/install script
    bash "$TOOLCHAIN_SCRIPT" "$ARCH" "$PREFIX"
    
    # Create tarball
    echo "Creating tarball $TARBALL..."
    tar -czf "$TARBALL" -C "/home/kraken" "sageos-toolchain-${ARCH}"
    
    echo "Done with $ARCH. Tarball created at $TARBALL"
done

echo "All requested toolchains processed successfully."
