FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04
ARG TARGETOS
ARG TARGETARCH

USER root
COPY ./storage/CacheServer /root/CacheServer
COPY ./storage/LivenessProbe /root/LivenessProbe
RUN chmod +x /root/CacheServer
RUN chmod +x /root/LivenessProbe

COPY ./check_space_overuse.sh /root/check_space_overuse.sh
RUN chmod +x /root/check_space_overuse.sh

RUN apt-get update && \
    apt-get install --no-install-recommends --no-install-suggests -yq -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" \
    wget ca-certificates curl ibverbs-providers fio iproute2 && \
    apt-get upgrade -yq -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" \
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
