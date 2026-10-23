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

FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04@sha256:104ae83764a5119017b8e8d6218fa0832b09df65aae7d5a6de29a85d813da2fb AS runtime

ARG UBUNTU_ARCHIVE_MIRROR=""

USER root

RUN if [ -n "${UBUNTU_ARCHIVE_MIRROR}" ]; then \
        sed -i "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_ARCHIVE_MIRROR}|g" /etc/apt/sources.list; \
    fi && \
    apt-get update && \
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
    libxml2 \
    libxxhash0 && \
    rm -rf /var/lib/apt/lists/* && \
    apt-get clean

COPY ./storage/LICENSE ./storage/NOTICE.txt /

COPY ./storage/CacheServer /root/CacheServer
COPY ./storage/LivenessProbe /root/LivenessProbe
COPY ./check_space_overuse.sh /root/check_space_overuse.sh

RUN chmod +x \
    /root/CacheServer \
    /root/LivenessProbe \
    /root/check_space_overuse.sh

WORKDIR /root
