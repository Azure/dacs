ARG ARG_DACS_BASE_CONTAINER=ubuntu:18.04

FROM $ARG_DACS_BASE_CONTAINER

USER root

RUN \
    apt-get update && \
    apt-get install -yq python3 \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libibverbs-dev \
    libssl-dev \
    curl \
    uuid-dev \
    ibverbs-providers

# Where the storage containers will be mounted to
RUN mkdir -p /app/mounts
RUN mkdir -p /app/lib
RUN mkdir -p /app/_cache
RUN mkdir -p /app/_cacheTemp

COPY src/subsystems/storage/stress_tests/launcher.py /app/launcher.py
COPY src/subsystems/storage/stress_tests/StorageClient.py /app/StorageClient.py
COPY bin/Release/native-storage/storage/CacheStorageClient /app/CacheStorageClient
COPY bin/Release/native-storage/storage/DeleteClient /app/DeleteClient
COPY src/subsystems/storage/stress_tests/storageIntercept.config /tmp/storageIntercept.config
# This copy requires the build to complete first, need to wire up this docker build invocation accordingly
COPY obj/Release/native/linux-x64/publish-storage/storage/libStorageIntercept.so /app/lib/libStorageIntercept.so

ENTRYPOINT ["python3", "/app/launcher.py"]
