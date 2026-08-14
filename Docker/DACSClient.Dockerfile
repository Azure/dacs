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

FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

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
    libboost-program-options-dev \
    libboost-system1.74-dev \
    libxxhash0

# Install Python dependencies for py_dacs_client and tests
RUN pip3 install --no-cache-dir \
    pytest \
    blobfile \
    boostedblob \
    azure-storage-blob \
    azure-identity

RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb \
    && apt-get update \
    && apt-get install azcopy -y

RUN mkdir -p /app

COPY LICENSE NOTICE.txt /

# Install build dependencies for wheel creation
RUN pip3 install --no-cache-dir setuptools wheel packaging

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
    pip3 install dist/*.whl && \
    rm -rf /tmp/dacs_client /bin/Debug
