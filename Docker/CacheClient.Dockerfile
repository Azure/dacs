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

# Build blobfuse2 with the dist_cache component from a pinned public commit.
FROM mcr.microsoft.com/oss/go/microsoft/golang:1.26@sha256:3638c0d1ad4b55d09cfa4e4583733303dffd6bcb7f97f2b5ba350d0d833dbabb AS blobfuse2-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates libfuse3-dev pkg-config build-essential && \
    rm -rf /var/lib/apt/lists/*
ARG BLOBFUSE2_REPO=https://github.com/Azure/azure-storage-fuse.git
ARG BLOBFUSE2_COMMIT=dd3a4461a3efdf679a3a06d0522b81d60edce781
RUN git init /src/blobfuse2 && \
    git -C /src/blobfuse2 remote add origin "${BLOBFUSE2_REPO}" && \
    git -C /src/blobfuse2 fetch --depth 1 origin "${BLOBFUSE2_COMMIT}" && \
    git -C /src/blobfuse2 checkout --detach FETCH_HEAD
WORKDIR /src/blobfuse2
RUN CGO_ENABLED=1 GOTOOLCHAIN=local go build -o /out/blobfuse2

FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04@sha256:104ae83764a5119017b8e8d6218fa0832b09df65aae7d5a6de29a85d813da2fb AS runtime

ARG UBUNTU_ARCHIVE_MIRROR=""
ARG MICROSOFT_REPO_FINGERPRINT=BC528686B50D79E339D3721CEB3E94ADBE1229CF

USER root

# Install runtime dependencies before copying application files so this layer
# remains cached when only DACS binaries change.
RUN if [ -n "${UBUNTU_ARCHIVE_MIRROR}" ]; then \
        sed -i "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_ARCHIVE_MIRROR}|g" /etc/apt/sources.list; \
    fi && \
    apt-get update && \
    apt-get upgrade -yq && \
    apt-get install -yq \
    python3 \
    python3-pip \
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
    libxxhash0 \
    libopenmpi-dev \
    openssh-client \
    openssh-server \
    fuse3 \
    libfuse3-3 && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean && \
    # The openssh-server postinst auto-generates private host keys at
    # /etc/ssh/ssh_host_*; drop them in the same layer so they never persist in
    # the image. sshd uses the per-job mounted key instead (see below).
    rm -f /etc/ssh/ssh_host_*

COPY requirements/cache-client.txt /tmp/cache-client-requirements.txt
COPY requirements/runai-model-streamer.txt /app/runai-model-streamer-requirements.txt
RUN --mount=type=secret,id=pip_index_url,required=false \
    if [ -f /run/secrets/pip_index_url ]; then \
        export PIP_INDEX_URL="$(cat /run/secrets/pip_index_url)"; \
    fi && \
    python3 -m pip install --no-cache-dir --require-hashes \
        -r /tmp/cache-client-requirements.txt && \
    rm /tmp/cache-client-requirements.txt

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
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean

# Install blobfuse2 (built above) and enable user_allow_other for FUSE mounts.
COPY --from=blobfuse2-builder /out/blobfuse2 /usr/local/bin/blobfuse2
RUN echo "user_allow_other" >> /etc/fuse.conf

# SSH setup for the Kubeflow MPI operator, mirroring the operator's own base
# image (https://github.com/kubeflow/mpi-operator/tree/master/build/base).
# The operator mounts a per-job SSH key Secret at /root/.ssh and starts each
# worker with `/usr/sbin/sshd -De` (reads the default /etc/ssh/sshd_config), so:
#   - point sshd's HostKey at the per-job key instead of baking static
#     /etc/ssh/ssh_host_* keys (removed above),
#   - listen on 2222 (the operator's SSH port),
#   - disable StrictModes and StrictHostKeyChecking and skip known_hosts, since
#     /root/.ssh is a read-only Secret mount.
ARG SSH_PORT=2222
RUN sed -i "s/[ #]\(.*StrictHostKeyChecking \).*/ \1no/" /etc/ssh/ssh_config && \
    echo "    UserKnownHostsFile /dev/null" >> /etc/ssh/ssh_config && \
    sed -i "s/[ #]\(.*Port \).*/ \1${SSH_PORT}/" /etc/ssh/ssh_config && \
    sed -i "s/#\(StrictModes \).*/\1no/" /etc/ssh/sshd_config && \
    sed -i "s/#\(Port \).*/\1${SSH_PORT}/" /etc/ssh/sshd_config && \
    echo "HostKey /root/.ssh/id_rsa" >> /etc/ssh/sshd_config && \
    mkdir -p /var/run/sshd /app/mounts /app/lib /app/_cache /app/_cacheTemp

COPY ./storage/LICENSE ./storage/NOTICE.txt /

COPY ./storage/cache-test/launcher.py /app/launcher.py
COPY ./storage/cache-test/StorageClient.py /app/StorageClient.py
COPY ./storage/cache-test/CacheStorageClient /app/CacheStorageClient
COPY ./storage/cache-test/DeleteClient /app/DeleteClient
COPY ./storage/cache-test/PerfClient /app/PerfClient

# Copy py_dacs_client library
COPY ./storage/py_dacs_client /app/py_dacs_client
COPY ./storage/libStorageDirect.so /app/py_dacs_client/libStorageDirect.so
COPY ./storage/libStorageIntercept.so /app/py_dacs_client/libStorageIntercept.so

RUN chmod +x \
    /app/CacheStorageClient \
    /app/DeleteClient \
    /app/PerfClient

WORKDIR /root
