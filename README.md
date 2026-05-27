# SageOS - The Modular Hybrid Operating System

SageOS is a hybrid operating system that combines a low-level C kernel with a high-level, SageLang-driven runtime. It is designed to be modular, portable, and extensible across multiple architectures.

## What SageOS Provides
- **Hybrid kernel architecture**: C for performance, SageLang for system services, shell logic, and runtime extensions.
- **Multi-architecture support**: x64, ARM64, and RV64 ports are maintained as submodules under `arch/`.
- **Custom SageLang runtime**: A bespoke MetalVM interpreter enables safe SageLang execution inside the OS.
- **Forked third-party networking stacks**: `lwip` and `mbedtls` are integrated as custom submodule forks to support networking and security.

## Why This Repository Exists
This repository is the central coordination point for SageOS development. It contains:
- `sageos_build/`: the shared system core and SageLang runtime.
- `arch/`: architecture-specific ports that reuse the core via nested submodules.
- `docs/`: project documentation and developer guides.
- `examples/`: sample SageLang programs and boot scripts.
- `setup_submodules.sh`: a trusted initializer that prevents infinite submodule recursion.

## Quick Start
```bash
git clone https://github.com/Night-Traders-Dev/SageOS.git
cd SageOS
./setup_submodules.sh
```

### Important
Do not use `git submodule update --init --recursive` directly on the root repository. SageOS relies on `./setup_submodules.sh` to properly configure local `core` references and disable redundant nested submodule clones.

## Repository Layout
- `sageos_build/`
  - `kernel/`: kernel boot flow, hardware abstraction, VFS bridge.
  - `sage_lang/`: SageLang compiler, runtime, and standard library support.
  - `actual_sagelang_build/`: host-side SageLang build utilities.
- `arch/`
  - `x64/`: x86_64 hardware and QEMU targets.
  - `arm64/`: ARM64 hardware and QEMU targets.
  - `rv64/`: RISC-V hardware and QEMU targets.
- `docs/`: documentation, architecture overviews, and guides.
- `examples/`: sample projects and demonstrations.
- `scripts/`: helper scripts and build utilities.

## Submodule Strategy
SageOS uses a nested submodule layout to share the core across ports while avoiding duplication.
- `setup_submodules.sh` initializes root submodules with `--remote`.
- It configures `arch/*/core` to reuse the local root repository as the core source.
- It disables redundant nested `lwip` and `mbedtls` updates inside architecture cores.

### Rebuilding the submodule graph
After pulling new changes:
```bash
git pull
./setup_submodules.sh
```

## Building and Running
Build and run the OS with the management script.

### Virtualized targets
```bash
./sageos.sh x64 virt build
./sageos.sh x64 virt run

./sageos.sh arm64 virt build
./sageos.sh arm64 virt run

./sageos.sh rv64 virt build
./sageos.sh rv64 virt run
```

### Hardware targets
```bash
./sageos.sh x64 lenovo_300e build
./sageos.sh x64 lenovo_300e run

./sageos.sh arm64 rpi4 run
```

## Documentation
- Read the root [SageOS Book](SageOS_Book.md) for architecture details and project philosophy.
- See `docs/README.md` for a documentation index and developer guide links.

## License
MIT License. See [LICENSE](LICENSE).
