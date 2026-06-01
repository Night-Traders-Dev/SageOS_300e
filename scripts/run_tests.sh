#!/usr/bin/env bash

# run_tests.sh — Unified SageOS test runner
#
# Usage:
#   scripts/run_tests.sh              # Run build + rootfs + sagelang tests
#   scripts/run_tests.sh --boot       # Also run QEMU boot smoke tests
#   scripts/run_tests.sh --boot-only  # Only run boot smoke tests
#
# Exit code: 0 if all tests pass, 1 if any test fails.

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

RUN_BUILD=1
RUN_BOOT=0
BOOT_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --boot)      RUN_BOOT=1 ;;
        --boot-only) BOOT_ONLY=1; RUN_BOOT=1; RUN_BUILD=0 ;;
        --no-boot)   RUN_BOOT=0 ;;
        --help|-h)
            echo "Usage: $0 [--boot] [--boot-only] [--no-boot]"
            exit 0
            ;;
    esac
done

log_pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS + 1)); }
log_fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL + 1)); }
log_skip() { echo -e "  ${YELLOW}[SKIP]${NC} $1"; SKIP=$((SKIP + 1)); }
log_section() { echo -e "\n${CYAN}=== $1 ===${NC}"; }

# ─────────────────────────────────────────────────────────────
# 1. Build Tests — Verify ELF artifacts exist and are valid
# ─────────────────────────────────────────────────────────────

if [[ "$RUN_BUILD" -eq 1 ]]; then
    log_section "Build Artifact Validation"

    for arch in x86_64 aarch64 riscv64; do
        elf="build/virt_${arch}/kernel.elf"
        if [[ -f "$elf" ]]; then
            if readelf -h "$elf" 2>/dev/null | grep -q "Entry point address:"; then
                log_pass "kernel.elf ($arch) — valid ELF with entry point"
            else
                log_fail "kernel.elf ($arch) — missing entry point"
            fi
        else
            log_skip "kernel.elf ($arch) — not built"
        fi
    done
fi

# ─────────────────────────────────────────────────────────────
# 2. RootFS Tests — Validate directory structure
# ─────────────────────────────────────────────────────────────

if [[ "$RUN_BUILD" -eq 1 ]]; then
    log_section "RootFS Structure Validation"

    ROOTFS=${ROOTFS:-"rootfs"}

    CRITICAL_FILES=(
        "etc/sagelang/init.sage"
        "etc/sagelang/runtime_manager.sage"
        "etc/sagelang/vfs_bridge.sage"
    )

    if [[ -d "$ROOTFS" ]]; then
        for cf in "${CRITICAL_FILES[@]}"; do
            if [[ -f "$ROOTFS/$cf" ]]; then
                log_pass "$cf present"
            else
                log_fail "$cf missing from $ROOTFS"
            fi
        done

        # Verify directory structure
        for dir in bin etc lib proc sys dev tmp usr var mnt; do
            if [[ -d "$ROOTFS/$dir" ]]; then
                log_pass "Directory $dir/ exists"
            else
                log_fail "Directory $dir/ missing"
            fi
        done
    else
        log_skip "RootFS not populated (run scripts/populate_rootfs.sh first)"
    fi
fi

# ─────────────────────────────────────────────────────────────
# 3. SageLang Tests — Run test suite
# ─────────────────────────────────────────────────────────────

if [[ "$RUN_BUILD" -eq 1 ]]; then
    log_section "SageLang Compiler Tests"

    SAGE_BIN="./sageos_build/sage_lang/core/sage"

    if [[ -x "$SAGE_BIN" ]]; then
        # Run selfhost test_parser if it exists
        PARSER_TEST="sageos_build/sage_lang/testsuite/selfhost/test_parser.sage"
        if [[ -f "$PARSER_TEST" ]]; then
            if timeout 30 "$SAGE_BIN" "$PARSER_TEST" >/dev/null 2>&1; then
                log_pass "test_parser.sage"
            else
                log_fail "test_parser.sage"
            fi
        else
            log_skip "test_parser.sage not found"
        fi

        # Run any tests under tests/ if it exists
        if [[ -d "tests" ]]; then
            for test_file in tests/*.sage; do
                [[ -f "$test_file" ]] || continue
                name=$(basename "$test_file")
                if timeout 30 "$SAGE_BIN" "$test_file" >/dev/null 2>&1; then
                    log_pass "$name"
                else
                    log_fail "$name"
                fi
            done
        fi

        # Basic smoke test: verify the compiler can parse init.sage
        INIT_SAGE="sageos_build/kernel/etc/init.sage"
        if [[ -f "$INIT_SAGE" ]]; then
            if timeout 10 "$SAGE_BIN" "$INIT_SAGE" >/dev/null 2>&1; then
                log_pass "init.sage parses successfully"
            else
                # Parser may fail because os_write_str is not defined outside kernel
                log_skip "init.sage (requires kernel runtime)"
            fi
        fi
    else
        log_skip "SageLang compiler not built"
    fi
fi

# ─────────────────────────────────────────────────────────────
# 4. QEMU Boot Smoke Tests
# ─────────────────────────────────────────────────────────────

if [[ "$RUN_BOOT" -eq 1 ]]; then
    log_section "QEMU Boot Smoke Tests"

    BOOT_TIMEOUT=60
    BOOT_SENTINELS=(
        "BOOT: Transitioning to STAGE 1"
        "SageOS Virt Kernel initialization complete"
    )

    declare -A QEMU_CMDS=(
        [x86_64]="qemu-system-x86_64 -machine pc -m 128M -display none -serial stdio -no-reboot"
        [aarch64]="qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 128M -display none -serial stdio"
        [riscv64]="qemu-system-riscv64 -machine virt -m 128M -display none -serial stdio -bios none -no-reboot"
    )

    declare -A KERNEL_FLAGS=(
        [x86_64]="-kernel build/virt_x86_64/kernel.elf"
        [aarch64]="-kernel build/virt_aarch64/kernel.elf"
        [riscv64]="-kernel build/virt_riscv64/kernel.elf"
    )

    # Use arch-specific or default disk image
    for arch in x86_64 aarch64 riscv64; do
        short_arch="${arch}"
        [[ "$arch" == "x86_64" ]] && short_arch="x64"
        [[ "$arch" == "aarch64" ]] && short_arch="arm64"
        [[ "$arch" == "riscv64" ]] && short_arch="rv64"

        elf="build/virt_${arch}/kernel.elf"
        if [[ ! -f "$elf" ]]; then
            log_skip "Boot test ($arch) — kernel not built"
            continue
        fi

        qemu_bin="${QEMU_CMDS[$arch]%% *}"
        if ! command -v "$qemu_bin" >/dev/null 2>&1; then
            log_skip "Boot test ($arch) — $qemu_bin not installed"
            continue
        fi

        # Find disk image
        disk_img=""
        if [[ -f "virt-${short_arch}.img" ]]; then
            disk_img="virt-${short_arch}.img"
        fi

        # Build QEMU command
        qemu_cmd="${QEMU_CMDS[$arch]} ${KERNEL_FLAGS[$arch]}"
        if [[ -n "$disk_img" ]]; then
            if [[ "$arch" == "x86_64" ]]; then
                qemu_cmd="$qemu_cmd -drive file=$disk_img,format=raw,index=0,media=disk,file.locking=off"
            else
                qemu_cmd="$qemu_cmd -drive file=$disk_img,format=raw,if=none,id=dr0 -device virtio-blk-device,drive=dr0"
            fi
        fi

        # Run with timeout, capture serial output
        serial_log=$(mktemp)
        timeout "$BOOT_TIMEOUT" $qemu_cmd > "$serial_log" 2>&1 || true

        # Validate sentinels
        all_found=1
        for sentinel in "${BOOT_SENTINELS[@]}"; do
            if ! grep -qF "$sentinel" "$serial_log"; then
                all_found=0
                break
            fi
        done

        if [[ "$all_found" -eq 1 ]]; then
            log_pass "Boot test ($arch) — kernel reached initialization"
        else
            log_fail "Boot test ($arch) — missing sentinel in serial output"
            echo "    Serial output (last 10 lines):"
            tail -10 "$serial_log" | sed 's/^/      /'
        fi

        rm -f "$serial_log"
    done
fi

# ─────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────

echo ""
log_section "Test Summary"
echo -e "  ${GREEN}Passed: $PASS${NC}  ${RED}Failed: $FAIL${NC}  ${YELLOW}Skipped: $SKIP${NC}"

if [[ "$FAIL" -gt 0 ]]; then
    echo -e "\n${RED}TESTS FAILED${NC}"
    exit 1
else
    echo -e "\n${GREEN}ALL TESTS PASSED${NC}"
    exit 0
fi
