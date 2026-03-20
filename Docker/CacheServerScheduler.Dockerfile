FROM mcr.microsoft.com/oss/go/microsoft/golang:1.24 as builder
ARG TARGETOS
ARG TARGETARCH

ENV CGO_ENABLED=0

WORKDIR /src
RUN --mount=target=/src,type=bind,source=. --mount=type=cache,target=/mnt/go-cache --mount=type=cache,target=/mnt/go-mod-cache \
    GOOS=${TARGETOS} GOARCH=${TARGETARCH} go build  -o /cacheserver-scheduler


FROM mcr.microsoft.com/mirror/docker/library/busybox:1.35

COPY --from=mcr.microsoft.com/mirror/docker/library/alpine:3.16 /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
COPY --from=builder /cacheserver-scheduler /cacheserver-scheduler
COPY ./cacheserver-scheduler-config.yaml /cacheserver-scheduler-config.yaml
RUN chmod +x /cacheserver-scheduler

ENTRYPOINT ["/cacheserver-scheduler"]