ARG ARG_DACS_BASE_CONTAINER=mcr.microsoft.com/mirror/docker/library/ubuntu:20.04

FROM $ARG_DACS_BASE_CONTAINER

USER root

RUN \
    apt-get update && \
    apt-get install -yq python3 \
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

# Where the storage containers will be mounted to
RUN mkdir -p /app/mounts
RUN mkdir -p /app/lib
RUN mkdir -p /app/_cache
RUN mkdir -p /app/_cacheTemp

COPY src/subsystems/storage/stress_tests/launcher.py /app/launcher.py
COPY src/subsystems/storage/stress_tests/StorageClient.py /app/StorageClient.py
COPY bin/Release/native-storage/storage/CacheStorageClient /app/CacheStorageClient
COPY bin/Release/native-storage/storage/DeleteClient /app/DeleteClient
COPY bin/Release/native-storage/storage/PerfClient /app/PerfClient
COPY src/subsystems/storage/stress_tests/storageIntercept.config /tmp/storageIntercept.config
COPY obj/Release/native/linux-x64/publish-storage/storage/libStorageIntercept.so /app/lib/libStorageIntercept.so

RUN apt-get install -yq libfuse-dev libfuse3-dev
COPY /bin/Release/native-storage/storage/rslex-fuse-cli /app/rslex-fuse-cli
COPY /bin/Release/native-storage/storage/dacsfs /app/dacsfs
