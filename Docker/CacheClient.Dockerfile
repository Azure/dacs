FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

RUN \
    apt-get update && \
    apt-get install -yq python3 \
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

# Where the storage containers will be mounted to
RUN mkdir -p /app/mounts
RUN mkdir -p /app/lib
RUN mkdir -p /app/_cache
RUN mkdir -p /app/_cacheTemp

COPY ./storage/cache-test/launcher.py /app/launcher.py
COPY ./storage/cache-test/StorageClient.py /app/StorageClient.py
COPY ./storage/cache-test/CacheStorageClient /app/CacheStorageClient
COPY ./storage/cache-test/DeleteClient /app/DeleteClient
COPY ./storage/cache-test/PerfClient /app/PerfClient
COPY ./storage/cache-test/storageIntercept.config /tmp/storageIntercept.config
COPY ./storage/libStorageIntercept.so /app/lib/libStorageIntercept.so

# Copy py_dacs_client library
COPY ./storage/py_dacs_client /app/py_dacs_client
COPY ./storage/libStorageDirect.so /app/py_dacs_client/libStorageDirect.so

RUN chmod +x /app/CacheStorageClient
RUN chmod +x /app/DeleteClient
RUN chmod +x /app/PerfClient

