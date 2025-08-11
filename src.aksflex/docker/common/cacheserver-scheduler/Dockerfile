FROM mcr.microsoft.com/oss/go/microsoft/golang:1.23 as builder

ENV CGO_ENABLED=0

WORKDIR /src
RUN --mount=target=/src,type=bind,source=. --mount=type=cache,target=/root/.cache/go-build go build -o /cacheserver-scheduler


FROM mcr.microsoft.com/mirror/docker/library/busybox:1.35

COPY --from=mcr.microsoft.com/mirror/docker/library/alpine:3.16 /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
COPY --from=builder /cacheserver-scheduler /cacheserver-scheduler
COPY ./cacheserver-scheduler-config.yaml /cacheserver-scheduler-config.yaml
RUN chmod +x /cacheserver-scheduler

ENTRYPOINT ["/cacheserver-scheduler"]