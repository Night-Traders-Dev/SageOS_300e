#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/sageos_build"
BOOT="$BUILD/boot"
KERNEL="$BUILD/kernel"
OBJ="$BUILD/obj"
IMG="$ROOT/sageos.img"
ESP="$BUILD/esp.img"

IMG_SIZE_MIB="${IMG_SIZE_MIB:-96}"
ESP_SIZE_MIB="${ESP_SIZE_MIB:-64}"
ESP_START_LBA="${ESP_START_LBA:-2048}"

restore_tty() {
    if [ -t 0 ]; then
        stty sane >/dev/null 2>&1 || true
    else
        (stty sane </dev/tty) >/dev/null 2>&1 || true
    fi
}

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: missing required tool: $1"
        exit 1
    fi
}

check_tools() {
    need clang
    need lld-link
    need ld.lld
    need llvm-nm
    need llvm-objcopy
    need mkfs.fat
    need mcopy
    need mmd
    need sgdisk
    need truncate
    need dd
}

build_kernel() {
    mkdir -p "$OBJ"

    echo "--- Syncing version header ---"
    local version
    version=$(cat "$ROOT/VERSION" | tr -d '\r\n')
    echo "#define SAGEOS_VERSION \"$version\"" > "$KERNEL/include/version.h"

    echo "--- Building kernel: modular freestanding x86_64 C/ASM ---"

    rm -f "$OBJ"/*.o "$OBJ"/*.obj 2>/dev/null || true

    local cfiles=()
    local objs=()

    for dir in \
        "$KERNEL/core" \
        "$KERNEL/drivers" \
        "$KERNEL/fs" \
        "$KERNEL/shell"
    do
        if [ -d "$dir" ]; then
            while IFS= read -r f; do
                cfiles+=("$f")
            done < <(find "$dir" -type f -name '*.c' | sort)
        fi
    done

    if [ "${#cfiles[@]}" -eq 0 ]; then
        echo "ERROR: no modular kernel C sources found."
        echo "Expected sources under:"
        echo "  $KERNEL/core"
        echo "  $KERNEL/drivers"
        echo "  $KERNEL/fs"
        echo "  $KERNEL/shell"
        exit 1
    fi

    for src in "${cfiles[@]}"; do
        local rel="${src#$KERNEL/}"
        local safe="${rel//\//_}"
        local obj="$OBJ/${safe%.c}.o"

        echo "  CC $rel"

        clang \
          -target x86_64-unknown-elf \
          -ffreestanding \
          -fno-stack-protector \
          -fno-pic \
          -fno-pie \
          -mno-red-zone \
          -msoft-float \
          -mno-sse \
          -mno-sse2 \
          -Wall \
          -Wextra \
          -I"$KERNEL/include" \
          -I"$KERNEL/core/sagelang" \
          -I"$KERNEL/core/sagelang/compiler" \
          -include "$KERNEL/include/sage_libc_shim.h" \
          -D__sageos__ \
          -DMETAL_STACK_SIZE=128 \
          -DMETAL_POOL_SIZE=256 \
          -DMETAL_STRING_POOL=16384 \
          -DMETAL_HEAP_SIZE=32768 \
          -DMETAL_CONST_POOL=256 \
          -DMETAL_NATIVE_MAX=64 \
          -DSAGEOS_FIRMWARE_I8042_FALLBACK="${SAGEOS_FIRMWARE_I8042_FALLBACK:-1}" \
          -c "$src" \
          -o "$obj"

        objs+=("$obj")
    done

    echo "  AS entry.S"

    clang \
      -target x86_64-unknown-elf \
      -ffreestanding \
      -fno-stack-protector \
      -fno-pic \
      -fno-pie \
      -mno-red-zone \
      -I"$KERNEL/include" \
      -c "$KERNEL/entry.S" \
      -o "$OBJ/entry.o"

    ld.lld \
      -nostdlib \
      -z max-page-size=0x1000 \
      -T "$KERNEL/linker.ld" \
      "$OBJ/entry.o" \
      "${objs[@]}" \
      -o "$BUILD/kernel.elf"

    llvm-objcopy -O binary --remove-section .bss "$BUILD/kernel.elf" "$BUILD/KERNEL.BIN"

    local kernel_start=""
    local kernel_end=""
    kernel_start="$(llvm-nm "$BUILD/kernel.elf" | awk '$3 == "__kernel_start" { print "0x" $1 }')"
    kernel_end="$(llvm-nm "$BUILD/kernel.elf" | awk '$3 == "__kernel_end" { print "0x" $1 }')"

    if [ -z "$kernel_start" ] || [ -z "$kernel_end" ]; then
        echo "ERROR: kernel extent symbols missing."
        exit 1
    fi

    local kernel_extent=$((kernel_end - kernel_start))
    truncate -s "$kernel_extent" "$BUILD/KERNEL.BIN"

    echo "[OK] $BUILD/kernel.elf"
    echo "[OK] $BUILD/KERNEL.BIN"
}

build_image() {
    check_tools

    mkdir -p "$BUILD" "$OBJ" "$BUILD/logs"

    echo "--- Cleaning stale objects/images ---"
    rm -f "$BUILD/BOOTX64.EFI"
    rm -f "$BUILD/kernel.elf" "$BUILD/KERNEL.BIN"
    rm -f "$OBJ/uefi_loader.obj" "$OBJ/kernel.o" "$OBJ/entry.o"
    rm -f "$ESP" "$IMG"

        # Patch 2: Warn when boot services remain active (the default).
    # Native i8042 and PIT IRQ0 drivers may conflict with UEFI firmware
    # interrupt ownership.  Use SAGEOS_EXIT_BOOT_SERVICES=1 for the strict
    # hardware path (keyboard reverts to native-only; ConIn no longer works).
    if [ "${SAGEOS_EXIT_BOOT_SERVICES:-0}" = "0" ]; then
        echo "WARN: SAGEOS_EXIT_BOOT_SERVICES=0 — UEFI boot services remain active."
        echo "      Firmware ConIn is primary; i8042 fallback is disabled by default."
        echo "      Set SAGEOS_FIRMWARE_I8042_FALLBACK=1 only for input diagnostics."
        echo "      Build with SAGEOS_EXIT_BOOT_SERVICES=1 for the strict native path."
    fi
    echo "--- Building UEFI loader: MS ABI PE/COFF + GOP handoff ---"

    clang \
      -target x86_64-windows-msvc \
      -ffreestanding \
      -fno-stack-protector \
      -fshort-wchar \
      -mno-red-zone \
      -DSAGEOS_EXIT_BOOT_SERVICES="${SAGEOS_EXIT_BOOT_SERVICES:-0}" \
      -Wall \
      -Wextra \
      -c "$BOOT/uefi_loader.c" \
      -o "$OBJ/uefi_loader.obj"

    lld-link \
      /subsystem:efi_application \
      /entry:EfiMain \
      /nodefaultlib \
      /out:"$BUILD/BOOTX64.EFI" \
      "$OBJ/uefi_loader.obj"

    # Compile SageLang shell sources -> bytecode -> C header
    echo "--- Compiling SageLang shell sources (SageShell) ---"
    if command -v sage > /dev/null 2>&1; then
        bash "$BUILD/scripts/compile_sage_shell.sh" sage "$KERNEL/shell"
    elif [ -x "$BUILD/sage_lang/sage" ]; then
        bash "$BUILD/scripts/compile_sage_shell.sh" "$BUILD/sage_lang/sage" "$KERNEL/shell"
    else
        echo "WARN: 'sage' not found on PATH and submodule not built — skipping SageShell bytecode compile."
        cat > "$KERNEL/shell/sage_shell_bytecode.h" <<'STUBEOF'
#pragma once
#include <stdint.h>
static const uint8_t sage_shell_bytecode[] = { 0xFF };
static const int sage_shell_bytecode_len = 1;
STUBEOF
    fi

    echo "--- Compiling SageLang VFS bridge (vfs_bridge) ---"
    if command -v sage > /dev/null 2>&1; then
        bash "$BUILD/scripts/compile_vfs_bridge.sh" sage
    elif [ -x "$BUILD/sage_lang/sage" ]; then
        bash "$BUILD/scripts/compile_vfs_bridge.sh" "$BUILD/sage_lang/sage"
    else
        echo "WARN: sage not found — using stub vfs_bridge_bytecode.h"
        cat > "$KERNEL/fs/vfs_bridge_bytecode.h" <<'STUBEOF'
#pragma once
#include <stdint.h>
static const uint8_t vfs_bridge_bytecode[] = { 0xFF };
static const int vfs_bridge_bytecode_len = 1;
STUBEOF
    fi

    build_kernel

    echo "--- Creating ESP FAT32 image ---"
    dd if=/dev/zero of="$ESP" bs=1M count="$ESP_SIZE_MIB" status=none
    mkfs.fat -F 32 -n SAGEOS "$ESP" >/dev/null

    mmd -i "$ESP" ::/EFI
    mmd -i "$ESP" ::/EFI/BOOT
    mcopy -i "$ESP" "$BUILD/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i "$ESP" "$BUILD/KERNEL.BIN" ::/KERNEL.BIN

    echo "--- Creating GPT disk image ---"
    truncate -s "${IMG_SIZE_MIB}M" "$IMG"

    sgdisk --clear "$IMG" >/dev/null
    sgdisk \
      --new=1:${ESP_START_LBA}:+${ESP_SIZE_MIB}M \
      --typecode=1:EF00 \
      --change-name=1:"EFI System" \
      "$IMG" >/dev/null

    dd if="$ESP" of="$IMG" bs=512 seek="$ESP_START_LBA" conv=notrunc status=none

    echo "--- Verifying GPT ---"
    sgdisk -v "$IMG"

    echo "--- Build complete ---"
    echo "Image:  $IMG"
    echo "UEFI:   $BUILD/BOOTX64.EFI"
    echo "Kernel: $BUILD/KERNEL.BIN"
}

qemu_run() {
    local ovmf=""

    export SAGEOS_EXIT_BOOT_SERVICES=1

    for f in \
        /usr/share/ovmf/OVMF.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/qemu/OVMF.fd
    do
        if [ -f "$f" ]; then
            ovmf="$f"
            break
        fi
    done

    if [ -z "$ovmf" ]; then
        echo "ERROR: could not find OVMF firmware."
        exit 1
    fi

    if [ ! -f "$IMG" ]; then
        echo "Image not found. Building first..."
        build_image
    fi

    echo "Disk device is $IMG"

    pkill -9 -f qemu-system-x86_64 >/dev/null 2>&1 || true
    restore_tty

    # -cpu Skylake-Client
    #   Exposes CPUID leaf 0x16 (processor frequency information) so
    #   sysinfo can read base/max MHz directly instead of falling back
    #   to RDTSC calibration.  Skylake-Client matches the ISA level of
    #   the Celeron N4020 (Gemini Lake, which is Goldmont Plus — a
    #   close enough model for diagnostic purposes).
    #
    # -device isa-debug-exit,iobase=0x501,iosize=2
    #   Allows the kernel 'exit' command to terminate QEMU cleanly.
    #   Writing 0x00 to port 0x501 exits with code 1.
    #
    # -drive id=hd0,...,media=disk
    #   Suppresses OVMF's "Disk device is ..." probe diagnostic.
    if ! qemu-system-x86_64 \
      -bios "$ovmf" \
      -cpu Skylake-Client \
      -drive id=hd0,file="$IMG",format=raw,media=disk,snapshot=on \
      -m 256M \
      -serial stdio \
      -device isa-debug-exit,iobase=0x501,iosize=2; then
        local rc=$?
        # rc=1 is the normal 'exit' command path (write 0x00 => (0<<1)|1 = 1)
        if [ "$rc" -ne 1 ]; then
            restore_tty
            return "$rc"
        fi
    fi

    restore_tty
}

flash_usb() {
    local dev="${1:-/dev/sdb}"

    if [ ! -f "$IMG" ]; then
        echo "Image not found. Building first..."
        build_image
    fi

    if [ ! -b "$dev" ]; then
        echo "ERROR: block device not found: $dev"
        exit 1
    fi

    case "$dev" in
        /dev/sda|/dev/nvme0n1|/dev/mmcblk0)
            echo "ERROR: refusing to flash likely system disk: $dev"
            exit 1
            ;;
    esac

    echo "=== SageOS Lenovo 300e Flasher ==="
    echo "Image:  $IMG"
    echo "Device: $dev"
    echo
    lsblk "$dev"
    echo
    echo "This will DESTROY all data on $dev."
    restore_tty

    if ! (: </dev/tty) 2>/dev/null; then
        echo "ERROR: flash confirmation requires an interactive terminal."
        exit 1
    fi

    printf "Type exactly YES to continue: " >/dev/tty
    IFS= read -r confirm </dev/tty || confirm=""
    echo

    if [ "$confirm" != "YES" ]; then
        echo "Aborted."
        exit 1
    fi

    echo "Unmounting mounted partitions..."
    while read -r part mountpoint; do
        if [ -n "${mountpoint:-}" ]; then
            sudo umount "/dev/$part" 2>/dev/null || true
        fi
    done < <(lsblk -ln -o NAME,MOUNTPOINT "$dev" | tail -n +2)

    echo "Flashing..."
    sudo dd if="$IMG" of="$dev" bs=4M status=progress conv=fsync

    sync
    sudo partprobe "$dev" 2>/dev/null || true

    echo
    echo "Done."
    lsblk "$dev"
}

clean_all() {
    rm -f "$IMG" "$ESP"
    rm -f "$BUILD/BOOTX64.EFI" "$BUILD/KERNEL.BIN" "$BUILD/kernel.elf"
    rm -f "$OBJ"/*.o "$OBJ"/*.obj 2>/dev/null || true
    echo "Cleaned Lenovo 300e build outputs."
}

status() {
    echo "Root:   $ROOT"
    echo "Image:  $IMG"
    echo "UEFI:   $BUILD/BOOTX64.EFI"
    echo "Kernel: $BUILD/KERNEL.BIN"
    echo
    ls -lh "$IMG" "$BUILD/BOOTX64.EFI" "$BUILD/KERNEL.BIN" "$BUILD/kernel.elf" 2>/dev/null || true
}

usage() {
    cat <<USAGE
SageOS Lenovo 300e unified build tool

Usage:
  ./lenovo_300e.sh build
  ./lenovo_300e.sh build-kernel
  ./lenovo_300e.sh qemu
  ./lenovo_300e.sh flash [/dev/sdX]
  ./lenovo_300e.sh all [/dev/sdX]
  ./lenovo_300e.sh clean
  ./lenovo_300e.sh status

Defaults:
  flash device: /dev/sdb
USAGE
}

cmd="${1:-}"
shift || true

case "$cmd" in
    build)
        build_image
        ;;
    build-kernel)
        check_tools
        # Generate bytecode header if not already present

        if [ ! -f "$KERNEL/fs/vfs_bridge_bytecode.h" ]; then
            if command -v sage > /dev/null 2>&1; then
                bash "$BUILD/scripts/compile_vfs_bridge.sh" sage
            else
                cat > "$KERNEL/fs/vfs_bridge_bytecode.h" <<'STUBEOF'
#pragma once
#include <stdint.h>
static const uint8_t vfs_bridge_bytecode[] = { 0xFF };
static const int vfs_bridge_bytecode_len = 1;
STUBEOF
            fi
        fi

        build_kernel
        ;;
    qemu)
        qemu_run
        ;;
    flash)
        flash_usb "${1:-/dev/sdb}"
        ;;
    all)
        build_image
        flash_usb "${1:-/dev/sdb}"
        ;;
    clean)
        clean_all
        ;;
    status)
        status
        ;;
    *)
        usage
        exit 1
        ;;
esac
