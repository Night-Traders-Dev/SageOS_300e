# SageOS Architecture: ARM64

**Status: Active Multitasking (v0.7.9 Standard)**

SageOS on ARM64 targets 64-bit ARMv8-A and newer processors. It focuses on modularity and leveraging architecture-specific features like Exception Levels (EL) for security and isolation.

## Target Hardware
- **Raspberry Pi 4 Model B**: Primary physical target.
- **QEMU `virt` machine**: Primary development and CI target.

## Execution Environment
- **Multitasking**: Full kernel-level multitasking enabled.
- **Preemption**: Cooperative preemption via interpreted bytecode polling.
- **Privilege**: EL1 (Kernel) and EL0 (User).
- **Isolation**: Managed via SGVM and hardware MMU (Stage 1/2).

## Memory Mapping
SageOS on ARM64 uses a 48-bit or 39-bit virtual address space.
- **Kernel Base**: `0xFFFF000000000000` (Typical high-memory kernel mapping).
- **Physical Load Address**:
  - RPi4: `0x80000`
  - Virt: `0x40000000`

## Drivers
- **UART**: PL011 PrimeCell UART for serial console output.
- **Interrupts**: GICv2 or GICv3 (Generic Interrupt Controller).
- **Timer**: ARM Generic Timer (arch-timer).

## Build Pipeline
The build pipeline utilizes `os.boot.build` from SageLang to generate:
1.  **Boot Stub**: Parks secondary cores and sets up the initial stack.
2.  **Runtime**: Minimal bare-metal Sage runtime.
3.  **Kernel**: The main OS logic.
