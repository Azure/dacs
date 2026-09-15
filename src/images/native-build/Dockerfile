# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Use Microsoft Container Registry (MCR) approved Ubuntu image
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04@sha256:104ae83764a5119017b8e8d6218fa0832b09df65aae7d5a6de29a85d813da2fb

# Docker BuildKit argument for multi-platform builds
ARG TARGETARCH
ARG RUST_VERSION=1.98.1
ARG RUSTUP_VERSION=1.29.0
ARG KUBEBUILDER_VERSION=4.10.1
ARG UBUNTU_ARCHIVE_MIRROR=""
ARG MICROSOFT_REPO_FINGERPRINT=BC528686B50D79E339D3721CEB3E94ADBE1229CF

USER root

ENV DEBIAN_FRONTEND=noninteractive \
  CMAKE_DEP_INSTALL_DIR=/dependency

RUN if [ -n "${UBUNTU_ARCHIVE_MIRROR}" ]; then \
        sed -i "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_ARCHIVE_MIRROR}|g" /etc/apt/sources.list; \
    fi && \
    apt-get update && \
    apt-get install -yq --no-install-recommends \
        apt-transport-https \
        ca-certificates \
        curl \
        gnupg && \
    curl --fail --silent --show-error --location \
        https://packages.microsoft.com/keys/microsoft.asc \
        --output /tmp/microsoft.asc && \
    export GNUPGHOME="$(mktemp -d)" && \
    actual_fingerprint="$(gpg --batch --show-keys --with-colons /tmp/microsoft.asc | \
        awk -F: '$1 == "fpr" { print $10; exit }')" && \
    if [ "$actual_fingerprint" != "$MICROSOFT_REPO_FINGERPRINT" ]; then \
        echo "Unexpected Microsoft repository signing key: $actual_fingerprint" >&2; \
        exit 1; \
    fi && \
    gpg --batch --yes --dearmor \
        --output /usr/share/keyrings/microsoft-prod.gpg \
        /tmp/microsoft.asc && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/microsoft-prod.gpg] https://packages.microsoft.com/ubuntu/22.04/prod jammy main" \
        > /etc/apt/sources.list.d/microsoft-prod.list && \
    rm -rf "$GNUPGHOME" /tmp/microsoft.asc && \
    apt-get update && \
    apt-get install -yq --no-install-recommends \
    # Core C++ development tools
    build-essential \
    clang \
    lld \
    cmake \
    ninja-build \
    gdb \
    ccache \
    pkg-config \
    # Required libraries for the project
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libibverbs-dev \
    libssl-dev \
    libboost-program-options-dev \
    libboost-system-dev \
    libgoogle-perftools-dev \
    libxml2-dev \
    uuid-dev \
    ibverbs-providers \
    fuse3 \
    libfuse3-dev \
    # Go Language
    msft-golang \
    # Python tools
    python3 \
    python3-dev \
    python3-pip \
    python3-venv \
    # Essential utilities
    wget \
    curl \
    ca-certificates \
    tar \
    git \
    net-tools && \
    # Clean up package cache to save disk space
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Architecture-specific library path linking for Google Performance Tools
RUN TARGETARCH=${TARGETARCH:-amd64} && \
    if [ "$TARGETARCH" = "amd64" ]; then \
        ln -s /usr/lib/x86_64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        ln -s /usr/lib/aarch64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so; \
    fi

COPY requirements.txt /tmp/native-build-requirements.txt
RUN --mount=type=secret,id=pip_index_url,required=false \
    if [ -f /run/secrets/pip_index_url ]; then \
        export PIP_INDEX_URL="$(cat /run/secrets/pip_index_url)"; \
    fi && \
    python3 -m venv /venv && . /venv/bin/activate && \
    python -m pip install --no-cache-dir --require-hashes \
        -r /tmp/native-build-requirements.txt && \
    rm /tmp/native-build-requirements.txt

# Set Rust installation paths for all users
ENV CARGO_HOME=/opt/cargo
ENV RUSTUP_HOME=/opt/rustup
ENV PATH="/opt/cargo/bin:${PATH}"

# Architecture-specific Rust installation
RUN TARGETARCH=${TARGETARCH:-amd64} && \
    if [ "$TARGETARCH" = "amd64" ]; then \
        RUST_TARGET=x86_64-unknown-linux-gnu; \
        RUSTUP_SHA256=4acc9acc76d5079515b46346a485974457b5a79893cfb01112423c89aeb5aa10; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        RUST_TARGET=aarch64-unknown-linux-gnu; \
        RUSTUP_SHA256=9732d6c5e2a098d3521fca8145d826ae0aaa067ef2385ead08e6feac88fa5792; \
    else \
        echo "Unsupported TARGETARCH: $TARGETARCH" >&2; \
        exit 1; \
    fi && \
    RUSTUP_INIT=/tmp/rustup-init && \
    curl --proto '=https' --tlsv1.2 --fail --silent --show-error --location \
        --output "$RUSTUP_INIT" \
        "https://static.rust-lang.org/rustup/archive/${RUSTUP_VERSION}/${RUST_TARGET}/rustup-init" && \
    echo "${RUSTUP_SHA256}  ${RUSTUP_INIT}" | sha256sum --check --strict && \
    chmod 0755 "$RUSTUP_INIT" && \
    "$RUSTUP_INIT" -y --no-modify-path --default-toolchain "${RUST_VERSION}-${RUST_TARGET}" && \
    rm "$RUSTUP_INIT" && \
    (${CARGO_HOME}/bin/rustup component remove rust-docs --toolchain "${RUST_VERSION}-${RUST_TARGET}" || true)

# Kubebuilder installation (for controller project)
RUN TARGETARCH=${TARGETARCH:-amd64} && \
    if [ "$TARGETARCH" = "amd64" ]; then \
        KUBEBUILDER_ARCH=amd64; \
        KUBEBUILDER_SHA256=d502bb409ef2f27ce7e69a6eddaf50501f876bb7fe4730c2fa087fe4e14a7e12; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        KUBEBUILDER_ARCH=arm64; \
        KUBEBUILDER_SHA256=de100c05583ba618ffb44240c63189f38a9abf9b3c959acb18fbf8a1d6ba3d33; \
    else \
        echo "Unsupported TARGETARCH: $TARGETARCH" >&2; \
        exit 1; \
    fi && \
    KUBEBUILDER_DOWNLOAD=/tmp/kubebuilder && \
    curl --proto '=https' --tlsv1.2 --fail --silent --show-error --location \
        --output "$KUBEBUILDER_DOWNLOAD" \
        "https://github.com/kubernetes-sigs/kubebuilder/releases/download/v${KUBEBUILDER_VERSION}/kubebuilder_linux_${KUBEBUILDER_ARCH}" && \
    echo "${KUBEBUILDER_SHA256}  ${KUBEBUILDER_DOWNLOAD}" | sha256sum --check --strict && \
    install -m 0755 "$KUBEBUILDER_DOWNLOAD" /usr/local/bin/.kubebuilder.new && \
    mv /usr/local/bin/.kubebuilder.new /usr/local/bin/kubebuilder && \
    rm "$KUBEBUILDER_DOWNLOAD"

# Install the OpenTelemetry Collector used by storage metrics integration tests.
ARG OTEL_COLLECTOR_VERSION=0.132.0
RUN TARGETARCH=${TARGETARCH:-amd64} && \
        curl -fsSL \
            "https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${OTEL_COLLECTOR_VERSION}/otelcol_${OTEL_COLLECTOR_VERSION}_linux_${TARGETARCH}.tar.gz" \
            -o /tmp/otelcol.tar.gz && \
        tar -xzf /tmp/otelcol.tar.gz -C /usr/local/bin otelcol && \
        chmod +x /usr/local/bin/otelcol && \
        rm /tmp/otelcol.tar.gz

# Configure Git to trust mounted repository directories
RUN git config --global --add safe.directory '*'

# Following is used to prebuild all DACS dependencies
# Architecture-specific CMake flags for cross-compilation
# Build with space optimizations to prevent "No space left on device" errors
COPY cmake $CMAKE_DEP_INSTALL_DIR/cmake/

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
    ninja -j$(nproc) || exit 1 && \
    # Clean up build artifacts immediately after successful build
    rm -rf CMakeFiles/ _deps/*/src/*-build/ _deps/*/src/*-stamp/ && \
    # Remove git repositories and submodules that were downloaded during build
    find . -name ".git" -type d -exec rm -rf {} + 2>/dev/null || true && \
    # Clean up any temporary files
    find . -name "*.tmp" -delete 2>/dev/null || true

#find all the dirs in /dependency and chmod 777,
#few dir name might have spaces in their name, so pass each line of the output of the find command to xargs instead of using space as separator
RUN find /dependency -type d -print0 | xargs -0 -I {} chmod 777 "{}"
