ARG ARG_DACS_BASE_CONTAINER=mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

FROM $ARG_DACS_BASE_CONTAINER

USER root

RUN \
    apt-get update && \
    apt-get install -yq python3 python3-pip \
    libfuse3-dev \
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
    ethtool \
    iproute2 \
    vim

RUN \
    python3 -m pip install --upgrade pip && \
    python3 -m pip install requests

# Where the storage containers will be mounted to
RUN mkdir -p /app/mounts
RUN mkdir -p /app/lib
RUN mkdir -p /app/_cache
RUN mkdir -p /app/_cacheTemp

COPY src/subsystems/storage/stress_tests/launcher.py /app/launcher.py
COPY src/subsystems/storage/stress_tests/StorageClient.py /app/StorageClient.py
COPY bin/Release/amd64/native-storage/storage/CacheStorageClient /app/CacheStorageClient
COPY bin/Release/amd64/native-storage/storage/DeleteClient /app/DeleteClient
COPY bin/Release/amd64/native-storage/storage/PerfClient /app/PerfClient
COPY src/subsystems/storage/stress_tests/storageIntercept.config /tmp/storageIntercept.config
COPY bin/Release/amd64/native-storage/storage/libStorageDirect.so /app/dacs_client/py_dacs_client/libStorageDirect.so
COPY bin/Release/amd64/native-storage/storage/libStorageIntercept.so /app/dacs_client/py_dacs_client/libStorageIntercept.so

COPY bin/Release/amd64/native-storage/storage/dacsfs /app/dacsfs
COPY src/subsystems/storage/dacs_client /app/dacs_client
