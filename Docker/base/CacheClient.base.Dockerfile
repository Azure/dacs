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

# Base image for CacheClient with pre-installed apt/pip packages
# This image is built once and pushed to ACR, avoiding slow apt downloads in CI
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

COPY --from=compliance LICENSE NOTICE.txt /

# Install all required apt packages.
# `apt-get upgrade` is included so each rebuild of this base image pulls the
# latest security patches for packages already present in the upstream image
# (e.g. libc6, libssl3, libgnutls30) -- not just the ones in our install list.
RUN apt-get update && \
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
    libboost-program-options-dev \
    libboost-system1.74-dev \
    libxxhash0 \
    libopenmpi-dev \
    openssh-client \
    openssh-server && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean && \
    # The openssh-server postinst auto-generates private host keys at
    # /etc/ssh/ssh_host_*; drop them in the same layer so they never persist in
    # the image (secret scanners flag them, and they would be shared across
    # every container). sshd uses the per-job mounted key instead (see below).
    rm -f /etc/ssh/ssh_host_*

# Install Python dependencies
RUN pip3 install --no-cache-dir \
    pytest \
    blobfile \
    boostedblob \
    azure-storage-blob \
    azure-identity \
    mpi4py

# Install azcopy
RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb && \
    dpkg -i packages-microsoft-prod.deb && \
    apt-get update && \
    apt-get install azcopy -y && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean && \
    rm -f packages-microsoft-prod.deb

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
    mkdir -p /var/run/sshd

# Create app directories
RUN mkdir -p /app/mounts /app/lib /app/_cache /app/_cacheTemp

WORKDIR /root
