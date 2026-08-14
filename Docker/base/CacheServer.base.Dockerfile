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

# Base image for CacheServer with pre-installed apt packages
# This image is built once and pushed to ACR, avoiding slow apt downloads in CI
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:22.04

USER root

COPY --from=compliance LICENSE NOTICE.txt /

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
