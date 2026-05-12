# Base image for CacheServer with pre-installed apt packages
# This image is built once and pushed to ACR, avoiding slow apt downloads in CI
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

# Install all required packages.
# `apt-get upgrade` is included so each rebuild of this base image pulls the
# latest security patches for packages already present in the upstream image
# (e.g. libc6, libssl3, libgnutls30) -- not just the ones in our install list.
RUN apt-get update && \
    apt-get upgrade --no-install-recommends --no-install-suggests -yq && \
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
