# Base image for CacheClient with pre-installed apt/pip packages
# This image is built once and pushed to ACR, avoiding slow apt downloads in CI
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

# Install all required apt packages
RUN apt-get update && \
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
    libxxhash0 && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean

# Install Python dependencies
RUN pip3 install --no-cache-dir \
    pytest \
    blobfile \
    boostedblob \
    azure-storage-blob \
    azure-identity

# Install azcopy
RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb && \
    dpkg -i packages-microsoft-prod.deb && \
    apt-get update && \
    apt-get install azcopy -y && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean && \
    rm -f packages-microsoft-prod.deb

# Create app directories
RUN mkdir -p /app/mounts /app/lib /app/_cache /app/_cacheTemp

WORKDIR /root
