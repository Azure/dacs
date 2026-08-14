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

# Use pre-built base image with packages installed (avoids slow apt downloads)
ARG DOCKER_REGISTRY=dacs.azurecr.io
FROM ${DOCKER_REGISTRY}/cache-client-base:latest

USER root

COPY ./storage/LICENSE ./storage/NOTICE.txt /

COPY ./storage/cache-test/launcher.py /app/launcher.py
COPY ./storage/cache-test/StorageClient.py /app/StorageClient.py
COPY ./storage/cache-test/CacheStorageClient /app/CacheStorageClient
COPY ./storage/cache-test/DeleteClient /app/DeleteClient
COPY ./storage/cache-test/PerfClient /app/PerfClient

# Copy py_dacs_client library
COPY ./storage/py_dacs_client /app/py_dacs_client
COPY ./storage/libStorageDirect.so /app/py_dacs_client/libStorageDirect.so
COPY ./storage/libStorageIntercept.so /app/py_dacs_client/libStorageIntercept.so

RUN chmod +x /app/CacheStorageClient
RUN chmod +x /app/DeleteClient
RUN chmod +x /app/PerfClient
