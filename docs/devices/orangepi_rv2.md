# Device Support: Orange Pi RV 2 (JH7110)

The Orange Pi RV 2 is a high-performance RISC-V 64-bit SBC based on the StarFive JH7110 SoC.

## Specifications
- **SoC**: StarFive JH7110.
- **CPU**: Quad-core SiFive U74 (RISC-V 64G C).
- **RAM**: up to 8GB LPDDR4.
- **UART**: 8250/16550 compatible (Base: `0x12030000`).

## Boot Procedure
1.  JH7110 boots from SPL (Secondary Program Loader).
2.  OpenSBI is typically used as the Supervisor Binary Interface.
3.  SageOS kernel is loaded by the bootloader (e.g., U-Boot) or OpenSBI.
4.  Standard RISC-V boot flow (`_start` at `0x40000000` or `0x80200000`).

## Build Action
```bash
./sageos.sh rv64 orangepi_rv2 build
```

## Running in QEMU
Since a dedicated JH7110 machine model is not always available in stock QEMU, the `virt` machine is used for emulation:
```bash
./sageos.sh rv64 orangepi_rv2 run
```
Note: This uses the standard `virt` machine with 4GB RAM. For serial output to work in QEMU `virt`, the build must be configured for the `virt` UART address (`0x10000000`). The `orangepi_rv2` build target uses the real hardware address (`0x12030000`).
