#!/usr/bin/env bash

set -e

# Configuration
ARCH=${1:-"x86_64"}
TARGET="${ARCH}-unknown-sageos"
HOST="$TARGET"
PREFIX="/usr"
INSTALL_DIR="/home/kraken/sageos-native-dist"

JOBS=$(nproc)
BINUTILS_VER="2.42"
GCC_VER="14.1.0"

# Add cross-compiler to PATH
if [ -d "/opt/sageos-toolchain/bin" ]; then
    export PATH="/opt/sageos-toolchain/bin:$PATH"
else
    export PATH="/home/kraken/sageos-toolchain/bin:$PATH"
fi

mkdir -p "$INSTALL_DIR"
BUILD_DIR="$(pwd)/toolchain_build"
cd "$BUILD_DIR"

echo "Building native Binutils ($ARCH)..."
mkdir -p build-native-binutils-${ARCH} && cd build-native-binutils-${ARCH}
../binutils-${BINUTILS_VER}/configure \
    --host="$HOST" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="/" \
    --disable-nls \
    --disable-werror
make MAKEINFO=true -j"$JOBS"
make MAKEINFO=true install DESTDIR="$INSTALL_DIR"
cd ..

echo "Building native GCC ($ARCH)..."
mkdir -p build-native-gcc-${ARCH} && cd build-native-gcc-${ARCH}
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
    --disable-libquadmath \
    --with-newlib
make MAKEINFO=true -j"$JOBS"
make MAKEINFO=true install DESTDIR="$INSTALL_DIR"
cd ..

echo "Native toolchain build complete! Files in $INSTALL_DIR"

echo "Native toolchain build complete!"
