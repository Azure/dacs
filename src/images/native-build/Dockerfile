FROM ubuntu:22.04

# Docker BuildKit argument for multi-platform builds
ARG TARGETARCH

USER root

ENV DEBIAN_FRONTEND=noninteractive \
  CMAKE_DEP_INSTALL_DIR=/dependency

COPY cmake $CMAKE_DEP_INSTALL_DIR/cmake/

# Add Microsoft package repository for msft-golang
ADD https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb /tmp/packages-microsoft-prod.deb

RUN apt-get update && \
    apt-get install -yq --no-install-recommends apt-transport-https ca-certificates && \
    dpkg -i /tmp/packages-microsoft-prod.deb && \
    rm /tmp/packages-microsoft-prod.deb && \
    apt-get update && \
    apt-get install -yq --no-install-recommends \
    wget \
    python3 \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libibverbs-dev \
    libssl-dev \
    libboost-program-options-dev \
    curl \
    uuid-dev \
    ibverbs-providers \
    sysstat \
    tar \
    net-tools \
    pciutils \
    cmake \
    docker.io \
    ethtool \
    pkg-config \
    iproute2 \
    vim \
    fuse3 \
    libfuse3-dev \
    apt-transport-https \
    gnupg \
    git \
    git-lfs \
    pass \
    gnupg-agent \
    software-properties-common \
    lsb-release \
    python3-dev \
    python3-pip \
    sudo \
    libboost-all-dev \
    libgoogle-perftools-dev \
    ccache \
    ninja-build \
    lld \
    clang \
    build-essential \
    gdb \
    locales \
    python3-venv \
    python3-setuptools \
    msft-golang && \
    # Clean up package cache to save disk space
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Configure locale to fix setlocale warnings
RUN locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    LANGUAGE=en_US:en

# Architecture-specific library path linking for Google Performance Tools
RUN TARGETARCH=${TARGETARCH:-amd64} && \
    if [ "$TARGETARCH" = "amd64" ]; then \
        sudo ln -s /usr/lib/x86_64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        sudo ln -s /usr/lib/aarch64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    fi

RUN python3 -m venv /venv && . /venv/bin/activate && \
    python -m pip install --no-cache-dir --upgrade pip setuptools opencensus-ext-azure h5py

# Architecture-specific Rust installation
RUN TARGETARCH=${TARGETARCH:-amd64} && \
    if [ "$TARGETARCH" = "amd64" ]; then \
        curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh -s -- -y && \
        /root/.cargo/bin/rustup self update && \
        /root/.cargo/bin/rustup component remove rust-docs || true; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh -s -- -y --default-toolchain stable-aarch64-unknown-linux-gnu; \
    fi
ENV PATH="/root/.cargo/bin:${PATH}"

# Configure Git to trust mounted repository directories
RUN git config --global --add safe.directory '*'

# Following is used to prebuild all DACS dependencies
# Architecture-specific CMake flags for cross-compilation
# Build with space optimizations to prevent "No space left on device" errors
RUN set -ex && cd $CMAKE_DEP_INSTALL_DIR && \
    # Clean up any existing build artifacts first
    rm -rf CMakeCache.txt CMakeFiles/ _deps/ && \
    # Set required environment variables for CMake
    export CMAKE_BINARY_DIR=$CMAKE_DEP_INSTALL_DIR/bin && \
    # Default TARGETARCH to amd64 if not set (for local builds without BuildKit)
    TARGETARCH=${TARGETARCH:-amd64} && \
    echo "Building for architecture: $TARGETARCH" && \
    # Configure with space-efficient settings and architecture-specific flags
    if [ "$TARGETARCH" = "amd64" ]; then \
        cmake -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -G Ninja $CMAKE_DEP_INSTALL_DIR/cmake || exit 1; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        cmake -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_SYSTEM_NAME=Linux \
             -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -G Ninja $CMAKE_DEP_INSTALL_DIR/cmake || exit 1; \
    fi && \
    echo "=== Checking for build.ninja make===" && \
    test -f build.ninja || (echo "ERROR: build.ninja not generated!" && exit 1) && \
    # Build with reduced parallelism to avoid memory/space pressure
    ninja -j2 || exit 1 && \
    # Clean up build artifacts immediately after successful build
    rm -rf CMakeFiles/ _deps/*/src/*-build/ _deps/*/src/*-stamp/ && \
    # Remove git repositories and submodules that were downloaded during build
    find . -name ".git" -type d -exec rm -rf {} + 2>/dev/null || true && \
    # Clean up any temporary files
    find . -name "*.tmp" -delete 2>/dev/null || true

#find all the dirs in /dependency and chmod 777,
#few dir name might have spaces in their name, so pass each line of the output of the find command to xargs instead of using space as separator
RUN find /dependency -type d -print0 | xargs -0 -I {} chmod 777 "{}"
