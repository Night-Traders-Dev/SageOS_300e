#!/usr/bin/env bash

# SageOS Development Environment Initialization Script
# Detects system and installs required dependencies for all supported architectures.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# 1. System Detection
HOST_ARCH=$(uname -m)
OS_TYPE=$(uname -s)

log_info "Detected Host Architecture: $HOST_ARCH"
log_info "Detected Operating System: $OS_TYPE"

if [[ "$OS_TYPE" != "Linux" ]]; then
    log_error "This script currently only supports Linux systems."
    exit 1
fi

# 2. Dependency List
# Base build tools
DEPS_BASE=(
    "build-essential"
    "cmake"
    "curl"
    "git"
    "python3"
    "python3-pip"
    "pkg-config"
    "mtools"
    "dosfstools"
    "gdisk"
    "util-linux"
)

# SageLang libraries
DEPS_SAGELANG=(
    "libcurl4-openssl-dev"
    "libssl-dev"
    "libvulkan-dev"
    "libglfw3-dev"
    "libgl1-mesa-dev"
    "vulkan-tools"
)

# Emulation
DEPS_QEMU=(
    "qemu-system-x86"
    "qemu-system-arm"
    "qemu-system-misc"
    "qemu-efi-aarch64"
    "qemu-efi-riscv64"
    "ovmf"
)

# Cross-compilers (Targeting all 3 supported archs)
DEPS_CROSS=(
    "gcc-aarch64-linux-gnu"
    "binutils-aarch64-linux-gnu"
    "gcc-x86-64-linux-gnu"
    "binutils-x86-64-linux-gnu"
    "gcc-riscv64-linux-gnu"
    "binutils-riscv64-linux-gnu"
)

# LLVM/Clang (Required for Lenovo 300e and UEFI builds)
DEPS_LLVM=(
    "clang"
    "lld"
    "llvm"
)

ALL_DEPS=("${DEPS_BASE[@]}" "${DEPS_SAGELANG[@]}" "${DEPS_QEMU[@]}" "${DEPS_CROSS[@]}" "${DEPS_LLVM[@]}")

# 3. Check for Package Manager
INSTALL_CMD=""
if command -v apt-get >/dev/null 2>&1; then
    INSTALL_CMD="apt-get install -y"
    log_info "Using apt-get package manager."
else
    log_error "Supported package manager (apt-get) not found. Please install dependencies manually."
    echo "Required packages: ${ALL_DEPS[*]}"
    exit 1
fi

# 4. Prompt User
echo -e "\n${YELLOW}SageOS Setup will install the following components:${NC}"
echo -e " - Base Build Tools (make, cmake, disk tools)"
echo -e " - SageLang Graphics & Network Libraries (Vulkan, GLFW, SSL)"
echo -e " - Emulation Suite (QEMU for x86, ARM, RISC-V)"
echo -e " - Cross-Compilers (AArch64, x86_64, RISC-V)"
echo -e " - LLVM/Clang Toolchain (UEFI builds)\n"

read -p "Do you want to proceed with the installation? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    log_warn "Installation aborted by user."
    exit 0
fi

# 5. Execution
log_step "Updating package lists..."
apt-get update

log_step "Installing dependencies (this may take a few minutes)..."
$INSTALL_CMD "${ALL_DEPS[@]}"

# 6. Final Setup
log_step "Initializing submodules..."
git submodule update --init --recursive

log_info "Building master SageLang compiler..."
(cd sageos_build/sage_lang/core && make)

log_info "System is ready for SageOS development!"
log_info "Try running: ./sageos.sh arm64 rpi4 run"
