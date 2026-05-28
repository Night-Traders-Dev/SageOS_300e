#!/usr/bin/env bash

# SageOS Toolchain Build Script
# Automates the creation of a cross-compiler for SageOS targets.

set -e

# Configuration
ARCH=${1:-"x86_64"}
TARGET="${ARCH}-unknown-sageos"
PREFIX="/opt/sageos-toolchain"
SYSROOT="${PREFIX}/sysroot"
JOBS=$(nproc)

# Versions
BINUTILS_VER="2.42"
GCC_VER="14.1.0"
NEWLIB_VER="4.4.0.20231231"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

log() { echo -e "${GREEN}[SageOS]${NC} $1"; }
step() { echo -e "${BLUE}>>${NC} $1"; }

if [ "$EUID" -ne 0 ]; then
  log "Please run as root (needed to install to $PREFIX)"
  exit 1
fi

mkdir -p "$PREFIX"
mkdir -p "$SYSROOT"

ROOT_DIR=$(pwd)
TOOLCHAIN_DIR="${ROOT_DIR}/toolchain"
BUILD_DIR="${ROOT_DIR}/toolchain_build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 1. Download Sources
step "Downloading sources..."
[ ! -f "binutils-${BINUTILS_VER}.tar.gz" ] && wget "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.gz"
[ ! -f "gcc-${GCC_VER}.tar.gz" ] && wget "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.gz"
[ ! -f "newlib-${NEWLIB_VER}.tar.gz" ] && wget "https://sourceware.org/pub/newlib/newlib-${NEWLIB_VER}.tar.gz"

step "Extracting sources..."
[ ! -d "binutils-${BINUTILS_VER}" ] && tar -xf "binutils-${BINUTILS_VER}.tar.gz"
[ ! -d "gcc-${GCC_VER}" ] && tar -xf "gcc-${GCC_VER}.tar.gz"
[ ! -d "newlib-${NEWLIB_VER}" ] && tar -xf "newlib-${NEWLIB_VER}.tar.gz"

# 2. Patch Sources
step "Patching Binutils..."
cp "binutils-${BINUTILS_VER}/config.sub" "binutils-${BINUTILS_VER}/config.sub.bak"
sed -i 's/sage\*) |/sageos\* | sage\* |/g' "binutils-${BINUTILS_VER}/config.sub"
# Note: Real patching would involve more robust sed or patch files

step "Patching GCC..."
cp "${TOOLCHAIN_DIR}/gcc/config/sageos.h" "gcc-${GCC_VER}/gcc/config/"
mkdir -p "gcc-${GCC_VER}/gcc/config/i386"
mkdir -p "gcc-${GCC_VER}/gcc/config/aarch64"
mkdir -p "gcc-${GCC_VER}/gcc/config/riscv"
cp "${TOOLCHAIN_DIR}/gcc/config/i386/sageos.h" "gcc-${GCC_VER}/gcc/config/i386/"
cp "${TOOLCHAIN_DIR}/gcc/config/aarch64/sageos.h" "gcc-${GCC_VER}/gcc/config/aarch64/"
cp "${TOOLCHAIN_DIR}/gcc/config/riscv/sageos.h" "gcc-${GCC_VER}/gcc/config/riscv/"

# Wire into config.gcc (simplified append)
if ! grep -q "sageos" "gcc-${GCC_VER}/gcc/config.gcc"; then
    cat >> "gcc-${GCC_VER}/gcc/config.gcc" <<EOF
x86_64-*-sageos*)
    tm_file="\${tm_file} i386/unix.h i386/att.h dbxelf.h elfos.h i386/i386elf.h i386/x86-64.h sageos.h i386/sageos.h"
    tmake_file="\${tmake_file} i386/t-i386elf t-svr4"
    ;;
aarch64-*-sageos*)
    tm_file="\${tm_file} dbxelf.h elfos.h aarch64/aarch64-elf.h aarch64/aarch64-freebsd.h sageos.h aarch64/sageos.h"
    ;;
riscv64-*-sageos*)
    tm_file="\${tm_file} dbxelf.h elfos.h riscv/riscv.h sageos.h riscv/sageos.h"
    ;;
EOF
fi

step "Patching Newlib..."
mkdir -p "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/syscalls.c" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/crt0.S" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/Makefile.inc" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"

# 3. Build Binutils
step "Building Binutils..."
mkdir -p build-binutils && cd build-binutils
../binutils-${BINUTILS_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-nls \
    --disable-werror
make -j"$JOBS"
make install
cd ..

# 4. Build GCC Stage 1
step "Building GCC Stage 1..."
mkdir -p build-gcc && cd build-gcc
../gcc-${GCC_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c \
    --without-headers \
    --disable-shared \
    --disable-threads \
    --disable-libssp \
    --disable-libgomp \
    --disable-libatomic \
    --disable-libquadmath \
    --disable-nls \
    --disable-werror
make all-gcc all-target-libgcc -j"$JOBS"
make install-gcc install-target-libgcc
cd ..

# 5. Build Newlib
step "Building Newlib..."
mkdir -p build-newlib && cd build-newlib
# Add SageOS to Newlib's configure.host or similar if needed
../newlib-${NEWLIB_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-newlib-supplied-syscalls \
    --enable-newlib-reent-small \
    --disable-newlib-fvwrite-in-streamio \
    --disable-newlib-wide-orient \
    --enable-newlib-nano-malloc \
    --disable-nls
make -j"$JOBS"
make install
cd ..

# 6. Build GCC Stage 2 (Final)
step "Building GCC Stage 2..."
cd build-gcc
../gcc-${GCC_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c \
    --disable-shared \
    --disable-threads \
    --disable-nls \
    --with-newlib
make -j"$JOBS"
make install
cd ..

log "Toolchain build complete! Installed to $PREFIX"
log "Add $PREFIX/bin to your PATH to use the toolchain."
