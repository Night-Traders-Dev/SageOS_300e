# Reproducible Builds

SageOS provides a Dockerfile for fully reproducible, hermetic builds of all kernel architectures.

## Quick Start

```bash
# Build the container (compiles everything from source)
docker build -t sageos-builder .

# Extract artifacts
mkdir -p output
docker run --rm -v $(pwd)/output:/out sageos-builder

# Artifacts will be in ./output/:
#   kernel-x86_64.elf
#   kernel-aarch64.elf
#   kernel-riscv64.elf
#   toolchain_versions.txt
#   build_info.txt
```

## What the Dockerfile Does

1. **Installs toolchains**: All three cross-compilation toolchains (x86_64, aarch64, riscv64) plus filesystem tools
2. **Builds SageLang**: Compiles the SageLang compiler from source
3. **Compiles bytecode**: Generates VFS bridge and shell bytecode artifacts
4. **Builds kernels**: Compiles kernel ELF binaries for all three architectures
5. **Populates rootfs**: Creates the FHS-compliant root filesystem
6. **Validates**: Runs the unified test suite (`scripts/run_tests.sh`)

## Toolchain Versions

The exact versions used are recorded in `toolchain_versions.txt` inside the output artifacts. This ensures any build difference can be traced to a toolchain change.

## Host Dependencies (Non-Docker)

If you prefer to build without Docker, install these packages on Ubuntu 24.04:

```bash
sudo apt-get install -y \
    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    gcc make mtools dosfstools btrfs-progs python3 git
```

Then follow the standard build process:
```bash
./setup_submodules.sh
./sageos.sh x64 virt build
./sageos.sh arm64 virt build
./sageos.sh rv64 virt build
```

## CI Integration

The GitHub Actions CI pipeline (`.github/workflows/build.yml`) installs the same packages listed above. The Dockerfile serves as the canonical reference for the build environment.
