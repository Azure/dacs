# Use pre-built base image with packages installed (avoids slow apt downloads)
ARG DOCKER_REGISTRY=dacs.azurecr.io
FROM ${DOCKER_REGISTRY}/cache-server-base:latest

USER root

COPY ./storage/CacheServer /root/CacheServer
COPY ./storage/LivenessProbe /root/LivenessProbe
RUN chmod +x /root/CacheServer
RUN chmod +x /root/LivenessProbe

COPY ./check_space_overuse.sh /root/check_space_overuse.sh
RUN chmod +x /root/check_space_overuse.sh

WORKDIR /root
