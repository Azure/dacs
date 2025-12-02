# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Cache Client Library Utils
# --------------------------------------------------------------------------------------------------------------------
#
# This module provides utility functions for managing memory buffers and configuring the DACS Cache client.
# It includes functions to allocate and release memory buffers, as well as to configure the managed identity mode for
# blob access tokens.
#
# Utility functions:
#    - alloc_buffer(size): Allocate a buffer of the specified size
#    - release_buffer(buffer): Release the allocated buffer and log the memory released
#    - configure_managed_identity_mode(azure_client_id): Configure managed identity mode for blob access token
#    - initialize_dacs_cache_client_config(intercept_path, sas_token): Initialize the dacs cache client with the given
#       intercept path and SAS token
#
# Notes:
#     - The configure_managed_identity_mode function is a temporary solution to update the token.
#       The token gets invalidated after an hour, but it is valid for 24 hours.
#       NB: There is ongoing work to move this logic inside the StorageIntercept library and to get & refresh the token
#         internally.
# ---------------------------------------------------------------------------------------------------------------------
import asyncio
import logging
import os
import requests
import sys
from ctypes import CDLL, c_char, c_size_t, c_void_p

logger = logging.getLogger(__name__)
handler = logging.StreamHandler(sys.stdout)
formatter = logging.Formatter(
    "%(asctime)s|DACSCacheClient|%(levelname)s|pid:%(process)d|%(funcName)s(%(lineno)s): %(message)s"
)
handler.setFormatter(formatter)
logger.addHandler(handler)
log_level = os.environ.get(
    "DACS_DATA_LOG_LEVEL", "INFO"
)  # Default logging level is INFO, unless overridden via env var
logger.setLevel(log_level)
logger.propagate = False  # Prevents duplicate logging if this module is imported in other modules

# Helper function to get the number of CPU cores
def get_cpu_cores() -> int:
    # Note: On dacs partial node skus, os.cpu_count() does not return correct number of cpus,
    #       use DACS_CPU_CORES environment variable if set
    cpu_cores = os.environ.get("DACS_CPU_CORES")
    if cpu_cores is None:
        cpu_cores = os.cpu_count()
        logger.info(f"DACS_CPU_CORES is not set, using os.cpu_count(): {cpu_cores}")
    else:
        logger.info(f"Number of CPU cores: {cpu_cores}")
    return int(cpu_cores)


def get_memory_limit() -> int:
    # Note: use DACS_MEMORY_MB env var if set, else default to 2 GB
    memory_limit_mb = int(os.environ.get("DACS_MEMORY_MB", 2048))
    logger.info(f"Container memory limit: {memory_limit_mb} MB")
    return memory_limit_mb * 1024 * 1024


# Internal function to get the current memory usage of the process
def get_current_memory_usage() -> int:
    with open("/proc/self/status") as f:
        for line in f:
            if "VmRSS" in line:
                return int(line.split()[1])
    return 0


libc = CDLL("/lib/x86_64-linux-gnu/libc.so.6")

libc.malloc.argtypes = (c_size_t,)
libc.malloc.restype = c_void_p

libc.free.argtypes = (c_void_p,)
libc.free.restype = None


def alloc_buffer(size: int) -> c_void_p:
    """Allocate a raw C buffer of the requested size.

    Ensures the incoming size is an int > 0, attempts to coerce from str/float, and
    raises helpful exceptions. Also validates malloc success before creating the
    Python object pointing at that memory region.
    """
    # Coerce to int if not already
    if not isinstance(size, int):
        try:
            size = int(size)  # may raise ValueError/TypeError which we convert below
        except Exception as e:
            raise TypeError(f"alloc_buffer: size must be an int-convertible value, got {type(size)}: {size!r}") from e

    if size <= 0:
        raise ValueError(f"alloc_buffer: size must be > 0, got {size}")

    ptr = libc.malloc(size)
    if not ptr:
        # malloc failed
        raise MemoryError(f"Failed to allocate buffer of size {size} bytes")

    # Create a ctypes char array *without* copying that memory
    return (c_char * size).from_address(ptr)


def release_buffer(buffer: c_void_p) -> None:
    if buffer is None:
        logger.warning("Attempted to release a None buffer. Skipping.")
        return
    if logger.level == logging.DEBUG:
        before_free = get_current_memory_usage()
    libc.free(buffer)
    if logger.level == logging.DEBUG:
        after_free = get_current_memory_usage()
        logger.debug(f"Buffer memory released:{before_free - after_free} KB")


class BoundedBufferManager:
    """
    A class to manage memory buffers with a bounded size limit.
    It allows acquiring and releasing buffers while ensuring that the total memory usage does not exceed the specified limit.
    """

    def __init__(self, max_memory_limit: int = 1024 * 1024 * 1024):
        self.max_memory_limit = max_memory_limit
        self.current_memory_usage = 0
        self.lock = asyncio.Lock()
        self.buffer_released_event = asyncio.Event()

    async def acquire_buffer(self, buf_size: int) -> c_void_p:
        while True:
            async with self.lock:
                if (self.current_memory_usage + buf_size <= self.max_memory_limit) or (buf_size > self.max_memory_limit):
                    buffer = alloc_buffer(buf_size)
                    if not buffer:
                        raise MemoryError("Failed to allocate buffer for reading blob chunk")

                self.current_memory_usage += buf_size
                usage_percent = (self.current_memory_usage / self.max_memory_limit) * 100
                logger.log(
                    logging.INFO if usage_percent > 90 else logging.DEBUG,
                    f"Buffer acquired. Current memory usage: {self.current_memory_usage} bytes ({usage_percent:.2f}%)",
                )
                return buffer

            await self.buffer_released_event.wait()
            self.buffer_released_event.clear()

    async def release_buffer(self, buffer: c_void_p) -> None:
        async with self.lock:
            buf_size = len(buffer)
            if buffer is None:
                logger.warning("Attempted to release a None buffer. Skipping.")
                return
            release_buffer(buffer)

            self.current_memory_usage -= buf_size
            usage_percent = (self.current_memory_usage / self.max_memory_limit) * 100
            logger.log(
                logging.INFO if usage_percent > 90 else logging.DEBUG,
                f"Buffer released. Current memory usage: {self.current_memory_usage} bytes ({usage_percent:.2f}%)",
            )

            self.buffer_released_event.set()


# NB: This is a temporary solution to update the token in the config file
# There is an ongoing effort to move this logic inside StorageIntercept library later
# as below token gets invalidated after an hour(But the token is valid for 24 hours)
# and the token is not refreshed internally.
def configure_managed_identity_mode(azure_client_id: str) -> None:
    logger.info("Fetching access token for {azure_client_id}")
    msi_endpoint = os.getenv("MSI_ENDPOINT")
    msi_secret = os.getenv("MSI_SECRET")

    response = requests.get(
        f"{msi_endpoint}?api-version=2017-09-01&resource=https://storage.azure.com&clientid={azure_client_id}",
        headers={"secret": msi_secret},
    )

    if response.status_code != 200:
        raise Exception(f"Failed to fetch access token:{response.text}")

    msi_token_response = response.json()
    logger.debug(f"MSI token response:{msi_token_response}")
    az_blob_access_token = msi_token_response.get("access_token")

    if not az_blob_access_token:
        raise Exception("Access token not found in the response")
    logger.info("Fetched access token and setting it in the environment variable")

    # Avoid the updation of the config file, rather set the environment variables
    os.environ["SI_AZBLOBACCESSTOKEN"] = az_blob_access_token
    os.environ["SI_AZBLOBSASTOKEN"] = ""

    return


def initialize_dacs_cache_client_config(intercept_path: str, sas_token: str) -> None:
    config_path = "/tmp/storageIntercept.config"
    with open(config_path, "w") as config_file:
        config_file.write(f"storagePath {intercept_path}\n")
        config_file.write("type blob\n")
        config_file.write("azBlobDynamicAccount true\n")
        config_file.write(f"azBlobSASToken {sas_token}\n")
        config_file.write("enableLargePrefetch false\n")
        config_file.write("prefetchWindowSizeInMB 64\n")
        config_file.write("streamingChunkSize 33554432\n")
        config_file.write("maxConnsPerBlobClient 84\n")
        config_file.write("attributesCacheEnable true\n")
        config_file.write("enableWrite false\n")
        config_file.write("enableWriteRemote false\n")
        config_file.write("enableWriteRemoteAsync false\n")
        # Check if CACHE_SERVER_LIST and CACHE_SERVER_PORT are set
        cache_server_list = os.environ.get("CACHE_SERVER_LIST")
        cache_server_port = os.environ.get("CACHE_SERVER_PORT")
        # If both are set, append and configure cache related configurations to /tmp/storageIntercept.config
        if cache_server_list and cache_server_port:
            config_file.write("cacheEnable true\n")
            config_file.write("cacheEnableRemote true\n")
            config_file.write(f"cacheServerPort {cache_server_port}\n")
            config_file.write(f"cacheServerList {cache_server_list}\n")
            config_file.write("cacheMaxUploadBufferSizePerProcessGB 400\n")
            config_file.write("cacheNumUploadThreadsPerProcess 80\n")
            config_file.write("attributesCacheUseLocalServer false\n")
            config_file.write("attributesCacheUseRemoteServer true\n")
            logger.debug(f"Configured cache related configurations in {config_path}")
        else:
            logger.warning(
                "CACHE_SERVER_LIST or CACHE_SERVER_PORT env variable is not set, working in Remote Blob mode only."
            )
