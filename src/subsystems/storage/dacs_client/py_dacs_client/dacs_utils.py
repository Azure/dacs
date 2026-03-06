# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Client Library Utils
# --------------------------------------------------------------------------------------------------------------------
#
# This module provides utility functions for managing memory buffers and configuring the DACS client.
# It includes functions to allocate and release memory buffers, as well as to configure the managed identity mode for
# blob access tokens.
#
# Utility functions:
#    - alloc_buffer(size): Allocate a buffer of the specified size
#    - release_buffer(buffer): Release the allocated buffer and log the memory released
#
# Internal functions:
#    - _initialize_dacs_client_config(intercept_path): Initialize the DACS client with authentication
#       (supports both SAS token and managed identity modes, internal use only)
#
# Notes:
#     - Authentication modes: SAS token (dev) via STORAGE_INTCPT_SAS_TOKEN env var, or managed identity (production) via AZURE_CLIENT_ID
#     - Workload identity is supported via AZURE_FEDERATED_TOKEN_FILE (set by AKS workload identity webhook)
#     - MSI endpoint and secret default to IMDS values for testing but can be overridden
# ---------------------------------------------------------------------------------------------------------------------
import asyncio
import logging
import os
import platform
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


# Detect architecture and load appropriate libc
machine = platform.machine()
if machine == "aarch64" or machine == "arm64":
    libc = CDLL("/lib/aarch64-linux-gnu/libc.so.6")
else:
    # Default to x86_64 (AMD64)
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

    logger.debug(f"Buffer memory allocated: {size} bytes at address {ptr}")

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
                if (self.current_memory_usage + buf_size <= self.max_memory_limit) or (
                    buf_size > self.max_memory_limit
                ):
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


def _initialize_dacs_client_config(intercept_path: str = None, write_mode=None) -> None:
    """Internal API: Initialize DACS client configuration.

    This is an internal function and should only be called from within the library.

    Args:
        intercept_path: Path to the intercept mount point
        write_mode: Write mode configuration (from WriteMode enum in dacs_adapter)
    """

    # Use process-specific config path to avoid race conditions in multi-process scenarios
    # The native library reads STORAGE_INTERCEPT_CONFIG_FILEPATH env var to find the config file
    pid = os.getpid()
    config_path = f"/tmp/storageIntercept.{pid}.config"
    os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] = config_path

    # Check if config file already exists and warn before overwriting
    if os.path.exists(config_path):
        logger.warning(f"Config file {config_path} already exists, overwriting!!!")

    with open(config_path, "w") as config_file:
        config_file.write(f"storagePath {intercept_path}\n")
        config_file.write("type blob\n")
        config_file.write("azBlobDynamicAccount true\n")
        config_file.write("azBlobDynamicContainer true\n")

        # Check if SAS token is provided (for dev operations), otherwise use managed identity
        sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN") if os.environ.get("USE_SAS_TOKEN", "false") == "true" else None
        access_token = os.environ.get("SI_azBlobAccessToken")
        if sas_token:
            config_file.write(f"azBlobSASToken {sas_token}\n")
            logger.info("Using SAS token for authentication")
        elif access_token:
            config_file.write(f"azBlobAccessToken {access_token}\n")
            logger.info("Using provided access token for authentication")
        else:
            logger.info("No access token provided, falling back to managed identity (default credential)")

            # Use managed identity for authentication
            azure_client_id = os.environ.get("AZURE_CLIENT_ID")

            # Validate that AZURE_CLIENT_ID is set (required for managed identity)
            if not azure_client_id:
                raise ValueError(
                    "Managed identity authentication requires AZURE_CLIENT_ID environment variable to be set. "
                    "Alternatively, set STORAGE_INTCPT_SAS_TOKEN for SAS token authentication."
                )

            # Check if workload identity is configured (AKS workload identity webhook injects these env vars)
            federated_token_file = os.environ.get("AZURE_FEDERATED_TOKEN_FILE")
            if federated_token_file:
                # Workload identity: pass federated token config to the native library
                # so it can handle token exchange and auto-refresh natively.
                azure_tenant_id = os.environ.get("AZURE_TENANT_ID")
                azure_authority_host = os.environ.get("AZURE_AUTHORITY_HOST", "https://login.microsoftonline.com")

                if not azure_tenant_id:
                    raise ValueError(
                        "Workload identity authentication requires AZURE_TENANT_ID environment variable to be set."
                    )

                config_file.write(f"azBlobIdentityClientId {azure_client_id}\n")
                config_file.write(f"azBlobFederatedTokenFile {federated_token_file}\n")
                config_file.write(f"azBlobTenantId {azure_tenant_id}\n")
                config_file.write(f"azBlobAuthorityHost {azure_authority_host}\n")

                logger.info(
                    "Using workload identity for authentication "
                    "(federated token file: %s, tenant: %s, authority host: %s, client id: %s)",
                    federated_token_file, azure_tenant_id, azure_authority_host, azure_client_id
                )
            else:
                # Traditional MSI: use MSI endpoint + secret for the native library's internal token fetching
                msi_endpoint = os.environ.get("MSI_ENDPOINT", "http://169.254.169.254/metadata/identity/oauth2/token")
                msi_secret = os.environ.get("MSI_SECRET", "dummy")

                config_file.write(f"azBlobIdentityClientId {azure_client_id}\n")
                config_file.write(f"azBlobMSIEndpoint {msi_endpoint}\n")
                config_file.write(f"azBlobMSISecret {msi_secret}\n")

                logger.info("Using managed identity (MSI endpoint) for authentication")

        config_file.write("enableLargePrefetch false\n")
        config_file.write("prefetchWindowSizeInMB 64\n")
        config_file.write("streamingChunkSize 33554432\n")
        config_file.write("maxConnsPerBlobClient 84\n")
        config_file.write("attributesCacheEnable true\n")

        # Configure write settings based on write_mode when cache is enabled
        # write_mode options:
        # - WRITE_DISABLED: Disable all write operations (read-only mode)
        # - WRITE_CACHE_ONLY: Write to local cache only, no remote blob writes
        # - WRITE_BLOB_ONLY: Write directly to remote blob, bypass cache
        # - WRITE_CACHE_BLOB_ASYNC: Write to cache first, async upload to blob in background
        # - WRITE_SYNC: Write to cache and blob synchronously (default)
        if write_mode is not None:
            write_mode_str = str(write_mode.value) if hasattr(write_mode, "value") else str(write_mode)
            logger.info(f"Configuring write mode (with cache): {write_mode_str}")

            if write_mode_str == "write_disabled":
                # Disable all write operations (read-only mode)
                config_file.write("enableWrite false\n")
                config_file.write("enableWriteRemote false\n")
                config_file.write("enableWriteRemoteAsync false\n")
            elif write_mode_str == "write_cache_only":
                # Write to cache only, no remote blob writes
                config_file.write("enableWrite true\n")
                config_file.write("enableWriteRemote false\n")
                config_file.write("enableWriteRemoteAsync false\n")
            elif write_mode_str == "write_blob_only":
                # Write directly to blob, bypass cache
                config_file.write("enableWrite true\n")
                config_file.write("enableWriteRemote true\n")
                config_file.write("enableWriteRemoteAsync false\n")
            elif write_mode_str == "write_cache_blob_async":
                # Write to cache first, async upload to blob
                config_file.write("enableWrite true\n")
                config_file.write("enableWriteRemote true\n")
                config_file.write("enableWriteRemoteAsync true\n")
            else:  # write_sync (default)
                # Write to cache and blob synchronously
                config_file.write("enableWrite true\n")
                config_file.write("enableWriteRemote true\n")
                config_file.write("enableWriteRemoteAsync false\n")
        else:
            # Default: disable write supports
            config_file.write("enableWrite false\n")
            config_file.write("enableWriteRemote false\n")
            config_file.write("enableWriteRemoteAsync false\n")

        # Cache configuration: Discovery or static server list
        cache_discovery_url = os.environ.get("CACHE_DISCOVERY_URL")
        cache_server_list = os.environ.get("CACHE_SERVER_LIST")
        cache_server_port = os.environ.get("CACHE_SERVER_PORT")
        
        cache_enabled = False
        
        if cache_discovery_url and cache_server_port:
            cache_enabled = True
            config_file.write(f"cacheServerPort {cache_server_port}\n")
            config_file.write("cacheServerDiscoveryEnabled true\n")
            config_file.write(f"cacheServerDiscoveryEndpoint {cache_discovery_url}\n")
            config_file.write("cacheServerDiscoveryIntervalInSeconds 300\n")
            logger.debug(f"Configured cache with discovery API: {cache_discovery_url}")
        elif cache_server_list and cache_server_port:
            cache_enabled = True
            config_file.write(f"cacheServerPort {cache_server_port}\n")
            config_file.write(f"cacheServerList {cache_server_list}\n")
            logger.debug(f"Configured cache with static server list: {cache_server_list}")
        
        if cache_enabled:
            config_file.write("cacheEnable true\n")
            config_file.write("cacheEnableRemote true\n")
            config_file.write("cacheRetryMaxAttempts 5\n")
            config_file.write("cacheRetryBaseDelayMs 100\n")
            config_file.write("cacheRetryMaxDelayMs 1000\n")
            config_file.write("cacheMaxUploadBufferSizePerProcessGB 400\n")
            config_file.write("cacheNumUploadThreadsPerProcess 80\n")
            config_file.write("attributesCacheUseLocalServer false\n")
            config_file.write("attributesCacheUseRemoteServer true\n")
        else:
            logger.warning("Cache not configured. Working in Remote Blob mode only.")
    
    logger.debug(f"Config file initialized: {config_path}")
