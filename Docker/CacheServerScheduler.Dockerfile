# Build the cacheserver-scheduler binary
FROM mcr.microsoft.com/oss/go/microsoft/golang:1.25 AS builder
ARG TARGETOS
ARG TARGETARCH

# Redirect the Go caches to /mnt so the BuildKit cache mounts below actually
# back them (Go's default $HOME cache locations are not mounted).
ENV GOMODCACHE=/mnt/go-mod-cache
ENV GOCACHE=/mnt/go-cache

WORKDIR /src

# Download modules in their own layer so it stays cached and is reused as long
# as go.mod/go.sum are unchanged, independent of source edits.
COPY go.mod go.sum ./
RUN --mount=type=cache,target=/mnt/go-mod-cache \
    go mod download

# CGO must be enabled so the Microsoft Go crypto backend (FIPS) can build.
COPY . .
RUN --mount=type=cache,target=/mnt/go-mod-cache \
    --mount=type=cache,target=/mnt/go-cache \
    CGO_ENABLED=1 GOOS=${TARGETOS:-linux} GOARCH=${TARGETARCH} go build -o /cacheserver-scheduler

FROM mcr.microsoft.com/azurelinux/base/core:3.0 AS dependency-install
RUN tdnf install -y --releasever 3.0 --installroot /staging \
    # ensure that libcrypto.so.X is available for dlopen for fips builds
    openssl-libs \
    && tdnf clean all \
    && rm -rf /staging/run /staging/var/log /staging/var/cache/tdnf

# Use distroless as minimal base image to package the scheduler binary
FROM mcr.microsoft.com/azurelinux/distroless/minimal:3.0
WORKDIR /
COPY --from=builder /cacheserver-scheduler /cacheserver-scheduler
COPY ./cacheserver-scheduler-config.yaml /cacheserver-scheduler-config.yaml
COPY --from=dependency-install /staging/ /

ENTRYPOINT ["/cacheserver-scheduler"]
