#!/usr/bin/env bash

set -e

# Configuration
ARCH=${1:-"aarch64"}
TARGET="${ARCH}-unknown-sageos"
HOST="$TARGET"
PREFIX="/opt/sageos-native"
SYSROOT="/opt/sageos-toolchain/sysroot"
JOBS=$(nproc)

BINUTILS_VER="2.42"
GCC_VER="14.1.0"

# Add cross-compiler to PATH
export PATH="/opt/sageos-toolchain/bin:$PATH"

mkdir -p "$PREFIX"
BUILD_DIR="$(pwd)/toolchain_build"
cd "$BUILD_DIR"

echo "Building native Binutils..."
mkdir -p build-native-binutils && cd build-native-binutils
../binutils-${BINUTILS_VER}/configure \
    --host="$HOST" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="/" \
    --disable-nls \
    --disable-werror
make -j"$JOBS"
make install
cd ..

echo "Building native GCC..."
mkdir -p build-native-gcc && cd build-native-gcc
../gcc-${GCC_VER}/configure \
    --host="$HOST" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="/" \
    --enable-languages=c \
    --disable-shared \
    --disable-threads \
    --disable-nls \
    --disable-libssp \
    --disable-libgomp \
    --disable-libatomic \
    --disable-libquadmath
make -j"$JOBS"
make install
cd ..

echo "Native toolchain build complete!"
