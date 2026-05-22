    # Extract binary sections into a flat image
    # Note: we truncate the binary to kernel_extent so the UEFI loader
    # allocates enough memory for the WHOLE kernel (including BSS).
    llvm-objcopy -O binary "$BUILD/kernel.elf" "$BUILD/KERNEL.BIN"
    
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
    echo "[OK] $BUILD/KERNEL.BIN (padded to $kernel_extent bytes)"
}
