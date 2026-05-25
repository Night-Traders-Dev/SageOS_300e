#!/usr/bin/env bash

# master shell script for SageOS build/flash/run management

set -e

# Configuration
SAGE_BIN="./sageos_build/sage_lang/core/sage"
EXAMPLES_DIR="./examples/boot"
BUILD_DIR="build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

show_help() {
    echo "SageOS Master Management Script"
    echo ""
    echo "Usage: $0 [arch] [device] [action]"
    echo ""
    echo "Architectures:"
    echo "  x64, arm64, rv64"
    echo ""
    echo "Devices:"
    echo "  arm64: rpi4, virt"
    echo "  x64:   q35, pc"
    echo "  rv64:  virt"
    echo ""
    echo "Actions:"
    echo "  build, flash, run"
    echo ""
    echo "Example:"
    echo "  $0 arm64 rpi4 run"
}

if [[ $# -lt 3 ]]; then
    show_help
    exit 1
fi

ARCH=$1
DEVICE=$2
ACTION=$3

# Ensure sage is built
if [[ ! -f "$SAGE_BIN" ]]; then
    log_info "Building SageLang..."
    (cd sageos_build/sage_lang/core && make)
fi

mkdir -p "$BUILD_DIR"

case "$ARCH" in
    arm64)
        case "$DEVICE" in
            rpi4)
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating RPi4 build environment..."
                    mkdir -p rpi4_boot_demo
                    $SAGE_BIN "$EXAMPLES_DIR/rpi4_demo.sage"
                    log_info "Building RPi4 kernel..."
                    chmod +x build_rpi4.sh
                    ./build_rpi4.sh
                    rm -rf "$BUILD_DIR/arm64_rpi4"
                    mv rpi4_boot_demo "$BUILD_DIR/arm64_rpi4"
                    rm build_rpi4.sh
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running RPi4 in QEMU..."
                    qemu-system-aarch64 -machine raspi4b -cpu cortex-a72 -m 1G -nographic -kernel "$BUILD_DIR/arm64_rpi4/kernel.elf"
                fi
                
                if [[ "$ACTION" == "flash" ]]; then
                    log_error "Flash action for RPi4 not yet implemented in this script."
                    exit 1
                fi
                ;;
            virt)
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating ARM64 virt build environment..."
                    mkdir -p boot_test_aarch64
                    $SAGE_BIN "$EXAMPLES_DIR/gen_build.sage"
                    log_info "Building ARM64 virt kernel..."
                    chmod +x build_kernel.sh
                    ./build_kernel.sh
                    rm -rf "$BUILD_DIR/arm64_virt"
                    mv boot_test_aarch64 "$BUILD_DIR/arm64_virt"
                    rm build_kernel.sh
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running ARM64 virt in QEMU..."
                    qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 128M -nographic -kernel "$BUILD_DIR/arm64_virt/kernel.elf"
                fi
                ;;
            *)
                log_error "Unknown device '$DEVICE' for arch '$ARCH'"
                exit 1
                ;;
        esac
        ;;
    x64)
        case "$DEVICE" in
            q35|pc)
                if [[ "$ACTION" == "build" ]]; then
                    log_info "Building x64 kernel (Stub)..."
                    # For now, we'll use a placeholder or generic sage compile if applicable
                    $SAGE_BIN --compile-bare "$EXAMPLES_DIR/hello_kernel.sage" -o "$BUILD_DIR/x64_kernel.elf" --target x86_64
                fi
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running x64 in QEMU..."
                    qemu-system-x86_64 -machine q35 -m 128M -nographic -kernel "$BUILD_DIR/x64_kernel.elf"
                fi
                ;;
            *)
                log_error "Unknown device '$DEVICE' for arch '$ARCH'"
                exit 1
                ;;
        esac
        ;;
    rv64)
        case "$DEVICE" in
            virt)
                if [[ "$ACTION" == "build" ]]; then
                    log_info "Building RV64 kernel (Stub)..."
                    $SAGE_BIN --compile-bare "$EXAMPLES_DIR/hello_kernel.sage" -o "$BUILD_DIR/rv64_kernel.elf" --target rv64
                fi
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running RV64 in QEMU..."
                    qemu-system-riscv64 -machine virt -m 128M -nographic -bios none -kernel "$BUILD_DIR/rv64_kernel.elf"
                fi
                ;;
            *)
                log_error "Unknown device '$DEVICE' for arch '$ARCH'"
                exit 1
                ;;
        esac
        ;;
    *)
        log_error "Unknown architecture '$ARCH'"
        exit 1
        ;;
esac
