# Base image for CacheServer with pre-installed apt packages
# This image is built once and pushed to ACR, avoiding slow apt downloads in CI
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

# Install all required packages
RUN apt-get update && \
    apt-get install --no-install-recommends --no-install-suggests -yq \
    wget \
    ca-certificates \
    curl \
    ibverbs-providers \
    fio \
    iproute2 \
    libjsoncpp-dev \
    libsystemd0 \
    libudev1 \
    libpam0g \
    libpam-modules-bin \
    libpam-modules \
    libpam-runtime \
    libxxhash0 && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean

WORKDIR /root
