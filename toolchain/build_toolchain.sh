#!/usr/bin/env bash

# SageOS Toolchain Build Script
# Automates the creation of a cross-compiler for SageOS targets.

set -e

# Configuration
ARCH=${1:-"x86_64"}
PREFIX=${2:-"/opt/sageos-toolchain"}
TARGET="${ARCH}-unknown-sageos"
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

# Check if PREFIX can be created/written by our current user
if [ ! -w "$(dirname "$PREFIX")" ] && [ ! -d "$PREFIX" ] && [ "$EUID" -ne 0 ]; then
  if [[ "$PREFIX" == "/opt/sageos-toolchain"* ]]; then
      log "Directory $(dirname "$PREFIX") is not writable and we are not root."
      DEFAULT_PREFIX="/home/kraken/sageos-toolchain-${ARCH}"
      log "Setting PREFIX to user-writable path: $DEFAULT_PREFIX"
      PREFIX="$DEFAULT_PREFIX"
      SYSROOT="${PREFIX}/sysroot"
  fi
fi

mkdir -p "$PREFIX"
mkdir -p "$SYSROOT"
export PATH="${PREFIX}/bin:${PATH}"

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

if [ -d "gcc-${GCC_VER}" ] && [ ! -f "gcc-${GCC_VER}/gmp/configure" ]; then
    step "Downloading GCC prerequisites (GMP, MPFR, MPC)..."
    cd "gcc-${GCC_VER}"
    ./contrib/download_prerequisites
    cd ..
fi

# 2. Patch Sources
step "Patching config.sub for all packages..."
for dir in binutils-${BINUTILS_VER} gcc-${GCC_VER} newlib-${NEWLIB_VER}; do
    if [ -f "$dir/config.sub" ]; then
        cp "$dir/config.sub" "$dir/config.sub.bak"
        # Add sageos to the accepted OS list. We look for uclinux as a known anchor.
        sed -i 's/| uclinux\*/| sageos* | uclinux\*/g' "$dir/config.sub"
    fi
done

step "Patching Binutils BFD..."
cp "binutils-${BINUTILS_VER}/bfd/config.bfd" "binutils-${BINUTILS_VER}/bfd/config.bfd.bak"
if ! grep -q "sageos" "binutils-${BINUTILS_VER}/bfd/config.bfd"; then
    python3 -c '
import sys
filename = f"binutils-{sys.argv[1]}/bfd/config.bfd"
with open(filename, "r") as f:
    content = f.read()

target_str = "#ifdef BFD64"

sageos_str = """#ifdef BFD64
  x86_64-*-sageos*)
    targ_defvec=x86_64_elf64_vec
    targ_selvecs="i386_elf32_vec iamcu_elf32_vec x86_64_elf32_vec"
    want64=true
    ;;
  aarch64-*-sageos*)
    targ_defvec=aarch64_elf64_le_vec
    targ_selvecs="aarch64_elf64_be_vec aarch64_elf32_le_vec aarch64_elf32_be_vec"
    want64=true
    ;;
  riscv64-*-sageos*)
    targ_defvec=riscv_elf64_vec
    targ_selvecs="riscv_elf32_vec"
    want64=true
    ;;"""

if target_str in content:
    content = content.replace(target_str, sageos_str, 1)
    with open(filename, "w") as f:
        f.write(content)
    print("[SageOS] successfully patched config.bfd")
else:
    print("[SageOS] ERROR: target pattern not found in config.bfd", file=sys.stderr)
    sys.exit(1)
' "$BINUTILS_VER"
fi

step "Patching Binutils LD..."
cp "binutils-${BINUTILS_VER}/ld/configure.tgt" "binutils-${BINUTILS_VER}/ld/configure.tgt.bak"
if ! grep -q "sageos" "binutils-${BINUTILS_VER}/ld/configure.tgt"; then
    sed -i '/case "${targ}" in/a \
x86_64-*-sageos*) targ_emul=elf_x86_64; targ_extra_emuls="elf_i386";; \
aarch64-*-sageos*) targ_emul=aarch64elf; targ_extra_emuls="aarch64elf32 aarch64elf32b aarch64elfb";; \
riscv64-*-sageos*) targ_emul=elf64lriscv; targ_extra_emuls="elf32lriscv";;' "binutils-${BINUTILS_VER}/ld/configure.tgt"
fi

step "Patching Binutils GAS..."
cp "binutils-${BINUTILS_VER}/gas/configure.tgt" "binutils-${BINUTILS_VER}/gas/configure.tgt.bak"
if ! grep -q "sageos" "binutils-${BINUTILS_VER}/gas/configure.tgt"; then
    sed -i '/case ${generic_target} in/a \
  aarch64-*-sageos*) fmt=elf ;; \
  i386-*-sageos*) fmt=elf ;; \
  riscv-*-sageos*) fmt=elf ;;' "binutils-${BINUTILS_VER}/gas/configure.tgt"
fi

step "Patching GCC..."
cp "${TOOLCHAIN_DIR}/gcc/config/sageos.h" "gcc-${GCC_VER}/gcc/config/"
mkdir -p "gcc-${GCC_VER}/gcc/config/i386"
mkdir -p "gcc-${GCC_VER}/gcc/config/aarch64"
mkdir -p "gcc-${GCC_VER}/gcc/config/riscv"
cp "${TOOLCHAIN_DIR}/gcc/config/i386/sageos.h" "gcc-${GCC_VER}/gcc/config/i386/"
cp "${TOOLCHAIN_DIR}/gcc/config/aarch64/sageos.h" "gcc-${GCC_VER}/gcc/config/aarch64/"
cp "${TOOLCHAIN_DIR}/gcc/config/riscv/sageos.h" "gcc-${GCC_VER}/gcc/config/riscv/"

# Wire into config.gcc (safely inject before fallback *) case)
if ! grep -q "sageos" "gcc-${GCC_VER}/gcc/config.gcc"; then
    python3 -c '
import sys
filename = f"gcc-{sys.argv[1]}/gcc/config.gcc"
with open(filename, "r") as f:
    content = f.read()

target_str = """*)
\techo "*** Configuration ${target} not supported" 1>&2"""

sageos_str = """x86_64-*-sageos*)
    tm_file="${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h i386/x86-64.h sageos.h i386/sageos.h"
    tmake_file="${tmake_file} i386/t-i386elf t-svr4"
    ;;
aarch64-*-sageos*)
    tm_file="${tm_file} elfos.h newlib-stdint.h aarch64/aarch64-elf.h aarch64/aarch64-freebsd.h sageos.h aarch64/sageos.h"
    ;;
riscv64-*-sageos*)
    tm_file="${tm_file} elfos.h newlib-stdint.h riscv/riscv.h sageos.h riscv/sageos.h"
    ;;
"""

if target_str in content:
    content = content.replace(target_str, sageos_str + target_str)
    with open(filename, "w") as f:
        f.write(content)
    print("[SageOS] successfully patched config.gcc")
else:
    print("[SageOS] ERROR: target pattern not found in config.gcc", file=sys.stderr)
    sys.exit(1)
' "$GCC_VER"
fi

# Wire into libgcc/config.host (safely inject before fallback/aarch64 target case)
if ! grep -q "sageos" "gcc-${GCC_VER}/libgcc/config.host"; then
    python3 -c '
import sys
filename = f"gcc-{sys.argv[1]}/libgcc/config.host"
with open(filename, "r") as f:
    content = f.read()

target_str = "aarch64*-*-elf | aarch64*-*-rtems*)"

sageos_str = """x86_64-*-sageos*)
	tmake_file="$tmake_file i386/t-crtstuff t-crtstuff-pic t-libgcc-pic"
	extra_parts="$extra_parts crtbegin.o crtend.o"
	;;
aarch64*-*-sageos*)
	extra_parts="$extra_parts crtbegin.o crtend.o crti.o crtn.o"
	tmake_file="${tmake_file} ${cpu_type}/t-aarch64"
	tmake_file="${tmake_file} ${cpu_type}/t-lse t-slibgcc-libgcc"
	tmake_file="${tmake_file} ${cpu_type}/t-softfp t-softfp"
	;;
riscv*-*-sageos*)
	extra_parts="$extra_parts crtbegin.o crtend.o crti.o crtn.o"
	tmake_file="${tmake_file} riscv/t-softfp${host_address} t-softfp riscv/t-elf riscv/t-elf${host_address}"
	;;
"""

if target_str in content:
    content = content.replace(target_str, sageos_str + target_str)
    with open(filename, "w") as f:
        f.write(content)
    print("[SageOS] successfully patched libgcc/config.host")
else:
    print("[SageOS] ERROR: target pattern not found in libgcc/config.host", file=sys.stderr)
    sys.exit(1)
' "$GCC_VER"
fi

step "Patching Newlib..."
mkdir -p "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/syscalls.c" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/crt0.S" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"
cp "${TOOLCHAIN_DIR}/newlib/libc/sys/sageos/Makefile.inc" "newlib-${NEWLIB_VER}/newlib/libc/sys/sageos/"

# Patch Newlib to recognize SageOS
if ! grep -q "sageos" "newlib-${NEWLIB_VER}/newlib/configure.host"; then
    sed -i '/case "${host}" in/a \  *-*-sageos*) \n    sys_dir=sageos \n    posix_dir= \n    has_ieee_754_libs=yes \n    ;;' "newlib-${NEWLIB_VER}/newlib/configure.host"
fi

# Ensure Makefile.inc in libc/sys includes sageos
if ! grep -q "sageos" "newlib-${NEWLIB_VER}/newlib/libc/sys/Makefile.inc"; then
    echo -e "if HAVE_LIBC_SYS_SAGEOS_DIR\ninclude %D%/sageos/Makefile.inc\nendif" >> "newlib-${NEWLIB_VER}/newlib/libc/sys/Makefile.inc"
fi

# 3. Build Binutils
step "Building Binutils ($ARCH)..."
mkdir -p build-binutils-${ARCH} && cd build-binutils-${ARCH}
../binutils-${BINUTILS_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-nls \
    --disable-werror \
    MAKEINFO=true
make -j"$JOBS" MAKEINFO=true
make install MAKEINFO=true
cd ..

# 4. Build GCC Stage 1
step "Building GCC Stage 1 ($ARCH)..."
mkdir -p "${PREFIX}/sysroot/usr/include"
mkdir -p build-gcc-${ARCH} && cd build-gcc-${ARCH}
../gcc-${GCC_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c \
    --without-headers \
    --with-newlib \
    --disable-shared \
    --disable-threads \
    --disable-libssp \
    --disable-libgomp \
    --disable-libatomic \
    --disable-libquadmath \
    --disable-nls \
    --disable-werror \
    MAKEINFO=true
make all-gcc all-target-libgcc -j"$JOBS" MAKEINFO=true
make install-gcc install-target-libgcc MAKEINFO=true
cd ..

# 5. Build Newlib
step "Building Newlib ($ARCH)..."
mkdir -p build-newlib-${ARCH} && cd build-newlib-${ARCH}
# Add SageOS to Newlib's configure.host or similar if needed
../newlib-${NEWLIB_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-libgloss \
    --enable-newlib-reent-small \
    --disable-newlib-fvwrite-in-streamio \
    --disable-newlib-wide-orient \
    --enable-newlib-nano-malloc \
    --disable-nls \
    MAKEINFO=true
make -j"$JOBS" MAKEINFO=true
make install MAKEINFO=true
cd ..

# 6. Build GCC Stage 2 (Final)
step "Building GCC Stage 2 ($ARCH)..."
cd build-gcc-${ARCH}
../gcc-${GCC_VER}/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c \
    --disable-shared \
    --disable-threads \
    --disable-nls \
    --with-newlib \
    MAKEINFO=true
make -j"$JOBS" MAKEINFO=true
make install MAKEINFO=true
cd ..

log "Toolchain build complete! Installed to $PREFIX"
log "Add $PREFIX/bin to your PATH to use the toolchain."
