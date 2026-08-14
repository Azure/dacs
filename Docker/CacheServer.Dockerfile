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
ARG DOCKER_REGISTRY=dacstest.azurecr.io
FROM ${DOCKER_REGISTRY}/cache-server-base:latest

USER root

COPY ./storage/LICENSE ./storage/NOTICE.txt /

COPY ./storage/CacheServer /root/CacheServer
COPY ./storage/LivenessProbe /root/LivenessProbe
RUN chmod +x /root/CacheServer
RUN chmod +x /root/LivenessProbe

COPY ./check_space_overuse.sh /root/check_space_overuse.sh
RUN chmod +x /root/check_space_overuse.sh

WORKDIR /root
