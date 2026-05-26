#!/usr/bin/env bash

# master shell script for SageOS build/flash/run management

set -e

# Export SAGE_PATH to use the repository's custom Sage standard library
export SAGE_PATH="$(pwd)/sageos_build/sage_lang/core/lib"

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
    echo "  x64:   q35, pc, lenovo_300e, virt"
    echo "  rv64:  virt, orangepi_rv2"
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
                    qemu-system-aarch64 -machine raspi4b -cpu cortex-a72 -m 4G -nographic -kernel "$BUILD_DIR/arm64_rpi4/kernel.elf"
                fi
                
                if [[ "$ACTION" == "flash" ]]; then
                    log_error "Flash action for RPi4 not yet implemented in this script."
                    exit 1
                fi
                ;;
            virt)
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating ARM64 virt interactive shell..."
                    $SAGE_BIN sageos_build/sage_lang/core/lib/os/examples/shell.sage aarch64
                    mkdir -p "$BUILD_DIR/arm64_virt"
                    cp /tmp/sageos_shell_aarch64/kernel.elf "$BUILD_DIR/arm64_virt/kernel.elf"
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running ARM64 virt in QEMU..."
                    qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 4G -display none -serial mon:stdio -no-reboot -kernel "$BUILD_DIR/arm64_virt/kernel.elf"
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
            virt)
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating x86_64 virt interactive shell..."
                    $SAGE_BIN sageos_build/sage_lang/core/lib/os/examples/shell.sage x86_64
                    mkdir -p "$BUILD_DIR/x64_virt"
                    cp /tmp/sageos_shell_x86_64/kernel.elf "$BUILD_DIR/x64_virt/kernel.elf"
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running x86_64 virt in QEMU..."
                    qemu-system-x86_64 -machine q35 -m 4G -display none -serial mon:stdio -no-reboot -kernel "$BUILD_DIR/x64_virt/kernel.elf"
                fi
                ;;
            q35|pc|lenovo_300e)
                if [[ "$DEVICE" == "lenovo_300e" && -f "arch/x64/lenovo_300e.sh" ]]; then
                    if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                        log_info "Using dedicated Lenovo 300e script for build..."
                        (cd arch/x64 && bash lenovo_300e.sh build)
                        mkdir -p "$BUILD_DIR/x64_lenovo_300e"
                        cp arch/x64/sageos-live.img "$BUILD_DIR/x64_lenovo_300e/"
                    fi
                    if [[ "$ACTION" == "run" ]]; then
                        log_info "Running Lenovo 300e in QEMU..."
                        (cd arch/x64 && bash lenovo_300e.sh qemu)
                    fi
                else
                    if [[ "$ACTION" == "build" ]]; then
                        log_info "Building x64 kernel for $DEVICE..."
                        $SAGE_BIN --compile-bare "$EXAMPLES_DIR/hello_kernel.sage" -o "$BUILD_DIR/x64_${DEVICE}_kernel.elf" --target x86_64
                    fi
                    if [[ "$ACTION" == "run" ]]; then
                        log_info "Running x64 $DEVICE in QEMU..."
                        qemu-system-x86_64 -machine q35 -m 4G -nographic -kernel "$BUILD_DIR/x64_${DEVICE}_kernel.elf"
                    fi
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
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating RISCV64 virt interactive shell..."
                    $SAGE_BIN sageos_build/sage_lang/core/lib/os/examples/shell.sage riscv64
                    mkdir -p "$BUILD_DIR/rv64_virt"
                    cp /tmp/sageos_shell_riscv64/kernel.elf "$BUILD_DIR/rv64_virt/kernel.elf"
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running RISCV64 virt in QEMU..."
                    qemu-system-riscv64 -machine virt -m 4G -display none -serial mon:stdio -bios none -no-reboot -kernel "$BUILD_DIR/rv64_virt/kernel.elf"
                fi
                ;;
            orangepi_rv2)
                if [[ "$ACTION" == "build" || "$ACTION" == "run" ]]; then
                    log_info "Generating $DEVICE build environment..."
                    mkdir -p "${DEVICE}_boot"
                    echo "import io" > gen_rv64.sage
                    echo "import os.boot.build as bb" >> gen_rv64.sage
                    echo "let arch = \"$DEVICE\"" >> gen_rv64.sage
                    echo "if arch == \"virt\": arch = \"riscv64\" end" >> gen_rv64.sage
                    echo "let build_script = bb.generate_build_script(arch, \"${DEVICE}_boot\", \"SageOS $DEVICE Booting...\")" >> gen_rv64.sage
                    echo "io.writefile(\"build_rv64.sh\", build_script)" >> gen_rv64.sage
                    $SAGE_BIN gen_rv64.sage
                    log_info "Building $DEVICE kernel..."
                    chmod +x build_rv64.sh
                    ./build_rv64.sh
                    rm -rf "$BUILD_DIR/rv64_$DEVICE"
                    mv "${DEVICE}_boot" "$BUILD_DIR/rv64_$DEVICE"
                    rm build_rv64.sh gen_rv64.sage
                fi
                
                if [[ "$ACTION" == "run" ]]; then
                    log_info "Running $DEVICE in QEMU..."
                    qemu-system-riscv64 -machine virt -m 4G -nographic -bios none -kernel "$BUILD_DIR/rv64_orangepi_rv2/kernel.elf"
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
