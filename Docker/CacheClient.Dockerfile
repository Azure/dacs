# Use pre-built base image with packages installed (avoids slow apt downloads)
ARG DOCKER_REGISTRY=dacs.azurecr.io
FROM ${DOCKER_REGISTRY}/cache-client-base:latest

USER root

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

