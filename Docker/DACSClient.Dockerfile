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

FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04@sha256:104ae83764a5119017b8e8d6218fa0832b09df65aae7d5a6de29a85d813da2fb

ARG MICROSOFT_REPO_FINGERPRINT=BC528686B50D79E339D3721CEB3E94ADBE1229CF

USER root

RUN \
    apt-get update && \
    apt-get install -yq python3 \
    python3-pip \
    python-is-python3 \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libibverbs-dev \
    libssl-dev \
    curl \
    uuid-dev \
    ibverbs-providers \
    sysstat \
    tar \
    fio \
    smartmontools \
    nvme-cli \
    wget \
    net-tools \
    pciutils \
    ethtool \
    vim \
    gnupg \
    libboost-program-options-dev \
    libboost-system1.74-dev \
    libxxhash0 && \
    rm -rf /var/lib/apt/lists/*

# Install the complete, hash-verified runtime, test, and wheel-build environment.
COPY Docker/requirements/dacs-client.txt /tmp/dacs-client-requirements.txt
RUN python3 -m pip install --no-cache-dir --require-hashes \
        -r /tmp/dacs-client-requirements.txt && \
    rm /tmp/dacs-client-requirements.txt

RUN curl --fail --silent --show-error --location \
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
    apt-get install --no-install-recommends -yq azcopy && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir -p /app

COPY LICENSE NOTICE.txt /

# Copy source files needed to build the wheel
COPY src/subsystems/storage/dacs_client/ /tmp/dacs_client/
COPY LICENSE NOTICE.txt /tmp/dacs_client/

# Create the expected directory structure and copy libStorageDirect.so
# setup.py resolves ../../../../bin/Debug/amd64/native-storage/storage/ from /tmp/dacs_client to /bin/Debug/amd64/...
RUN mkdir -p /bin/Debug/amd64/native-storage/storage
COPY bin/Debug/amd64/native-storage/storage/libStorageDirect.so /bin/Debug/amd64/native-storage/storage/

# Build and install the wheel
RUN cd /tmp/dacs_client && \
    rm -rf dist/ build/ *.egg-info && \
    export BUILD_ARCHITECTURE=amd64 && \
    export BUILD_CHANNEL=dev && \
    python3 setup.py bdist_wheel && \
    python3 -m pip install --no-deps dist/*.whl && \
    rm -rf /tmp/dacs_client /bin/Debug
