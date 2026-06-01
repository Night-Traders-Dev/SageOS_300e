# SageOS Reproducible Build Environment
#
# Build:   docker build -t sageos-builder .
# Run:     docker run --rm -v $(pwd)/output:/out sageos-builder
#
# This Dockerfile produces kernel ELF binaries for all three architectures
# and validates them with the unified test runner.

FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

# Install all build dependencies in a single layer
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
    # Cross-compilation toolchains
    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    # Native compiler for SageLang
    gcc make \
    # Filesystem tools
    mtools dosfstools btrfs-progs \
    # Utilities
    python3 git bash coreutils findutils \
    && rm -rf /var/lib/apt/lists/*

# Record toolchain versions for reproducibility
RUN echo "=== Build Toolchain Versions ===" > /toolchain_versions.txt && \
    gcc --version | head -1 >> /toolchain_versions.txt && \
    x86_64-linux-gnu-gcc --version | head -1 >> /toolchain_versions.txt && \
    aarch64-linux-gnu-gcc --version | head -1 >> /toolchain_versions.txt && \
    riscv64-linux-gnu-gcc --version | head -1 >> /toolchain_versions.txt && \
    python3 --version >> /toolchain_versions.txt && \
    mtools --version 2>&1 | head -1 >> /toolchain_versions.txt && \
    cat /etc/os-release | grep PRETTY_NAME >> /toolchain_versions.txt

WORKDIR /sageos

# Copy source tree
COPY . .

# Set up SageLang path
ENV SAGE_PATH=/sageos/sageos_build/sage_lang/core/lib

# Stage 1: Build SageLang compiler
RUN cd sageos_build/sage_lang/core && make -j$(nproc)

# Stage 2: Compile Sage components to bytecode
RUN bash scripts/compile_vfs_bridge.sh ./sageos_build/sage_lang/core/sage && \
    bash scripts/compile_sage_shell.sh ./sageos_build/sage_lang/core/sage

# Stage 3: Generate build scripts for all architectures
RUN mkdir -p build && \
    ./sageos_build/sage_lang/core/sage scripts/build_virt.sage

# Stage 4: Build kernels for all three architectures
RUN bash build/virt_x86_64/build.sh
RUN bash build/virt_aarch64/build.sh
RUN bash build/virt_riscv64/build.sh

# Stage 5: Populate rootfs
RUN bash scripts/populate_rootfs.sh

# Stage 6: Run validation tests
RUN bash scripts/run_tests.sh --no-boot

# Collect artifacts
RUN mkdir -p /artifacts && \
    cp build/virt_x86_64/kernel.elf /artifacts/kernel-x86_64.elf && \
    cp build/virt_aarch64/kernel.elf /artifacts/kernel-aarch64.elf && \
    cp build/virt_riscv64/kernel.elf /artifacts/kernel-riscv64.elf && \
    cp /toolchain_versions.txt /artifacts/ && \
    cp VERSION /artifacts/ && \
    echo "Build completed at: $(date -u)" > /artifacts/build_info.txt

# Final stage: minimal image with just the artifacts
FROM scratch AS artifacts
COPY --from=builder /artifacts/ /

# Default entrypoint copies artifacts to /out mount
FROM ubuntu:24.04 AS runner
COPY --from=builder /artifacts/ /artifacts/
CMD ["sh", "-c", "cp -r /artifacts/* /out/ 2>/dev/null && echo 'Artifacts copied to /out/' || echo 'Mount a volume at /out to extract artifacts: docker run --rm -v ./output:/out sageos-builder'"]
