
# =============================================================================
#
# THIS IS A GENERATED DOCKERFILE - DO NOT MODIFY
#
# Make changes to Dockerfile.template 
#
# =============================================================================
FROM ubuntu:22.04

# Docker BuildKit argument for multi-platform builds
ARG TARGETARCH

USER root

ENV DEBIAN_FRONTEND=noninteractive \
  CMAKE_DEP_INSTALL_DIR=/dependency \
  NUGET_CREDENTIALPROVIDER_SESSIONTOKENCACHE_ENABLED=true

COPY cmake $CMAKE_DEP_INSTALL_DIR/cmake/

RUN \
    apt-get update && \
    apt-get install -yq python3 \
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
    wget \
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
    python3 \
    python3-dev \
    python3-pip \
    sudo \
    libboost-all-dev \
    libgoogle-perftools-dev \
    ninja-build \
    lld \
    clang \
    build-essential \
    gdb \
    locales && \
    # Clean up package cache to save disk space
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
  
RUN apt-get update && \
    apt-get install -y python3-venv python3-pip python3-setuptools && \
    # Clean up package cache after Python installation
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Configure locale to fix setlocale warnings
RUN locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    LANGUAGE=en_US:en

# Architecture-specific library path linking for Google Performance Tools
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        sudo ln -s /usr/lib/x86_64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        sudo ln -s /usr/lib/aarch64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    fi

RUN python3 -m venv /venv && . /venv/bin/activate &&  \
  python -m pip install --upgrade pip setuptools opencensus-ext-azure h5py && \
  # Clean up pip cache to save space
  python -m pip cache purge

# Architecture-specific Rust installation
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh -s -- -y && \
        /root/.cargo/bin/rustup self update && \
        /root/.cargo/bin/rustup component remove rust-docs || true; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh -s -- -y --default-toolchain stable-aarch64-unknown-linux-gnu; \
    fi
ENV PATH="/root/.cargo/bin:${PATH}"

# Architecture-specific Go installation
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        wget https://go.dev/dl/go1.20.5.linux-amd64.tar.gz && \
        rm -rf /usr/local/go && tar -C /usr/local -xzf go1.20.5.linux-amd64.tar.gz && \
        rm go1.20.5.linux-amd64.tar.gz; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        wget https://go.dev/dl/go1.20.5.linux-arm64.tar.gz && \
        rm -rf /usr/local/go && tar -C /usr/local -xzf go1.20.5.linux-arm64.tar.gz && \
        rm go1.20.5.linux-arm64.tar.gz; \
    fi
ENV PATH="/usr/local/go/bin:${PATH}"

RUN wget -qO- https://aka.ms/install-artifacts-credprovider.sh | bash

# Architecture-specific .NET SDK installation
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        cd /opt && wget https://builds.dotnet.microsoft.com/dotnet/Sdk/6.0.428/dotnet-sdk-6.0.428-linux-x64.tar.gz && \
        mkdir dotnet && \
        tar -C dotnet -xf dotnet-sdk-6.0.428-linux-x64.tar.gz && \
        rm -f dotnet-sdk-6.0.428-linux-x64.tar.gz; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        curl -sSL https://dot.net/v1/dotnet-install.sh | bash /dev/stdin --version 6.0.428 --install-dir /opt/dotnet && \
        ln -sf /usr/share/dotnet/* /opt/dotnet/ || true; \
    fi

# Architecture-specific PowerShell installation
RUN if [ "$TARGETARCH" = "amd64" ]; then \
        wget https://github.com/PowerShell/PowerShell/releases/download/v7.5.0/powershell_7.5.0-1.deb_amd64.deb && \
        dpkg -i powershell_7.5.0-1.deb_amd64.deb || true && \
        apt-get install -f -y && \
        rm powershell_7.5.0-1.deb_amd64.deb; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        wget https://github.com/PowerShell/PowerShell/releases/download/v7.5.0/powershell-7.5.0-linux-arm64.tar.gz && \
        mkdir -p /opt/microsoft/powershell/7 && \
        tar -xf powershell-7.5.0-linux-arm64.tar.gz -C /opt/microsoft/powershell/7 && \
        chmod +x /opt/microsoft/powershell/7/pwsh && \
        ln -s /opt/microsoft/powershell/7/pwsh /usr/bin/pwsh && \
        rm powershell-7.5.0-linux-arm64.tar.gz; \
    fi && \
    # Clean up after PowerShell installation
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

ENV PATH="/opt/dotnet:$PATH"

RUN curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash && \
    # Clean up after Azure CLI installation
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN az extension add --name azure-devops

# Configure Git to trust mounted repository directories
RUN git config --global --add safe.directory '*'

# Following is used to prebuild all DACS dependencies
# Architecture-specific CMake flags for cross-compilation
# Build with space optimizations to prevent "No space left on device" errors
RUN cd $CMAKE_DEP_INSTALL_DIR && \
    # Clean up any existing build artifacts first
    rm -rf CMakeCache.txt CMakeFiles/ _deps/ && \
    # Configure with space-efficient settings and architecture-specific flags
    if [ "$TARGETARCH" = "amd64" ]; then \
        cmake -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_VERBOSE_MAKEFILE=OFF \
             -G Ninja $CMAKE_DEP_INSTALL_DIR/cmake; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        cmake -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_SYSTEM_NAME=Linux \
             -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
             -DCMAKE_VERBOSE_MAKEFILE=OFF \
             -G Ninja $CMAKE_DEP_INSTALL_DIR/cmake; \
    fi && \
    # Build with reduced parallelism to avoid memory/space pressure
    ninja -j2 && \
    # Clean up build artifacts immediately after successful build
    rm -rf CMakeFiles/ _deps/*/src/*-build/ _deps/*/src/*-stamp/ && \
    # Remove git repositories and submodules that were downloaded during build
    find . -name ".git" -type d -exec rm -rf {} + 2>/dev/null || true && \
    # Clean up any temporary files
    find . -name "*.tmp" -delete 2>/dev/null || true

#find all the dirs in /dependency and chmod 777,
#few dir name might have spaces in their name, so pass each line of the output of the find command to xargs instead of using space as separator
RUN find /dependency -type d -print0 | xargs -0 -I {} chmod 777 "{}"

RUN cd ~

