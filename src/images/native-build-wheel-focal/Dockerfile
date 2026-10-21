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

FROM mcr.microsoft.com/mirror/docker/library/ubuntu:20.04@sha256:8feb4d8ca5354def3d8fce243717141ce31e2c428701f6682bd2fafe15388214

ENV DEBIAN_FRONTEND=noninteractive \
    CMAKE_DEP_INSTALL_DIR=/dependency \
    CC=clang \
    CXX=clang++ \
    PATH="/opt/python/bin:${PATH}"

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang-18 \
        ninja-build ccache pkg-config \
        libjsoncpp-dev libcurl4-openssl-dev libibverbs-dev libssl-dev \
        libboost-program-options-dev libboost-system-dev \
        libgoogle-perftools-dev libxml2-dev uuid-dev libfuse3-dev \
        zlib1g-dev libbz2-dev liblzma-dev libffi-dev libsqlite3-dev \
        ca-certificates curl git xz-utils && \
    ln -s "/usr/lib/$(gcc -dumpmachine)/libprofiler.so" /usr/lib/libprofiler.so && \
    rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-18 /usr/local/bin/clang && \
    ln -s /usr/bin/clang++-18 /usr/local/bin/clang++

# Build the packaging interpreter against Focal, without replacing its libc.
RUN curl -fsSL https://www.python.org/ftp/python/3.12.15/Python-3.12.15.tar.xz \
        -o /tmp/python.tar.xz && \
    echo "c2c4321961fab0fb999d66e0cecf521c2ab3994c7992873ea99e306c1094fd5a  /tmp/python.tar.xz" | sha256sum -c - && \
    mkdir /tmp/python-source && \
    tar -xJf /tmp/python.tar.xz -C /tmp/python-source --strip-components=1 && \
    cd /tmp/python-source && \
    ./configure --prefix=/opt/python --with-ensurepip=install && \
    make -j4 && make install && \
    rm -rf /tmp/python-source /tmp/python.tar.xz

COPY focal-requirements.txt /tmp/focal-requirements.txt
RUN --mount=type=secret,id=pip_index_url,required=false \
    if [ -f /run/secrets/pip_index_url ]; then \
        export PIP_INDEX_URL="$(cat /run/secrets/pip_index_url)"; \
    fi && \
    python3 -m pip install --no-cache-dir --require-hashes \
        -r /tmp/focal-requirements.txt && \
    rm /tmp/focal-requirements.txt

COPY cmake /dependency/cmake
WORKDIR /dependency
RUN cmake -S cmake -B . -G Ninja -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --parallel 4
RUN git config --global --add safe.directory /workspace
