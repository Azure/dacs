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

RUN if [ -n "${UBUNTU_ARCHIVE_MIRROR}" ]; then \
        sed -i "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_ARCHIVE_MIRROR}|g" /etc/apt/sources.list; \
    fi && \
    apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates python3 python3-pip && \
    rm -rf /var/lib/apt/lists/*

COPY storage/LICENSE storage/NOTICE.txt /
COPY storage/python-wheel/*.whl /tmp/wheels/
RUN --mount=type=secret,id=pip_index_url,required=false \
    if [ -f /run/secrets/pip_index_url ]; then \
        export PIP_INDEX_URL="$(cat /run/secrets/pip_index_url)"; \
    fi && \
    python3 -m pip install --no-cache-dir /tmp/wheels/*.whl && \
    rm -rf /tmp/wheels && \
    python3 -m pip check && \
    python3 -c "from py_dacs_client.dacs_client import DACSCacheClient"
