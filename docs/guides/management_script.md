# Master Management Script: sageos.sh

The `sageos.sh` script is the central command tool for building and running SageOS.

## Commands

### Build
Builds the kernel for a specific architecture and device.
```bash
./sageos.sh <arch> <device> build
```

### Run
Builds (if necessary) and runs the kernel in QEMU.
```bash
./sageos.sh <arch> <device> run
```

### Flash
(Planned) Flashes the built image to physical media.
```bash
./sageos.sh <arch> <device> flash
```

## Supported Targets
- `arm64 rpi4`
- `arm64 virt`
- `x64 q35`
- `x64 virt`
- `rv64 virt`
