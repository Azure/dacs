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

# Build the manager binary
FROM mcr.microsoft.com/oss/go/microsoft/golang:1.26 AS builder
ARG TARGETOS
ARG TARGETARCH

WORKDIR /workspace

# Configure Go to use /mnt for caches to avoid filling root filesystem
ENV GOMODCACHE=/mnt/go-mod-cache
ENV GOCACHE=/mnt/go-cache

# Copy the Go Modules manifests
COPY go.mod go.mod
COPY go.sum go.sum
# cache deps before building and copying source so that we don't need to re-download as much
# and so that source changes don't invalidate our downloaded layer
RUN --mount=type=cache,target=/mnt/go-mod-cache \
    --mount=type=cache,target=/mnt/go-cache \
    go mod download

# Copy the Go source (relies on .dockerignore to filter)
COPY . .

# Build
# the GOARCH has no default value to allow the binary to be built according to the host where the command
# was called. For example, if we call make docker-build in a local env which has the Apple Silicon M1 SO
# the docker BUILDPLATFORM arg will be linux/arm64 when for Apple x86 it will be linux/amd64. Therefore,
# by leaving it empty we can ensure that the container and binary shipped on it will have the same platform.
RUN --mount=type=cache,target=/mnt/go-mod-cache \
    --mount=type=cache,target=/mnt/go-cache \
    CGO_ENABLED=1 GOOS=${TARGETOS:-linux} GOARCH=${TARGETARCH} go build -o manager cmd/main.go

FROM mcr.microsoft.com/azurelinux/base/core:3.0 AS dependency-install
RUN tdnf install -y --releasever 3.0 --installroot /staging \
    # ensure that libcrypto.so.X is available for dlopen for fips builds
    openssl-libs \
    && tdnf clean all \
    && rm -rf /staging/run /staging/var/log /staging/var/cache/tdnf


# Use distroless as minimal base image to package the manager binary
# Refer to https://github.com/GoogleContainerTools/distroless for more details
FROM mcr.microsoft.com/azurelinux/distroless/minimal:3.0
WORKDIR /
COPY --from=builder /workspace/manager .
COPY --from=builder /workspace/internal/config/cache-controller-config.yaml /cache-controller-config.yaml
COPY LICENSE NOTICE.txt /
COPY --from=dependency-install /staging/ /
USER 65532:65532

ENTRYPOINT ["/manager"]
