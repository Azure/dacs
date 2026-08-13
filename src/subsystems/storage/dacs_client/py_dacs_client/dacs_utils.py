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
#       (supports SAS token or access token or managed identity or Azure Identity SDK token fetching)
# ---------------------------------------------------------------------------------------------------------------------
import asyncio
import ctypes
import logging
import os
import platform
import sys
import threading
from ctypes import CDLL, c_char, c_size_t, c_ssize_t, c_void_p
from enum import Enum
from typing import Optional

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


class WriteMode(Enum):
    """
    Enumeration of write modes for DACS write operations.

    WRITE_DISABLED: Disable write operations, read-only mode
    WRITE_CACHE_ONLY: Write to cache only, no blob storage
    WRITE_BLOB_ONLY: Write to blob storage only, bypass cache
    WRITE_CACHE_BLOB_ASYNC: Write to cache first, async update blob storage
    WRITE_SYNC: Write to cache and blob storage synchronously
    """

    WRITE_DISABLED = "write_disabled"
    WRITE_CACHE_ONLY = "write_cache_only"
    WRITE_BLOB_ONLY = "write_blob_only"
    WRITE_CACHE_BLOB_ASYNC = "write_cache_blob_async"
    WRITE_SYNC = "write_sync"


# =============================================================================
# Shared Event Loop for Sync Wrappers
# =============================================================================
# This provides a dedicated event loop running in a background thread.
# Sync wrappers use this loop to run async code without creating/destroying
# loops on each call (avoiding asyncio.run() overhead).
# Thread-safe and fork-safe via lazy initialization with PID detection.
# =============================================================================

class _SharedLoopManager:
    """
    Manages a dedicated asyncio event loop in a background thread for sync operations.
    
    Thread-safe and fork-safe: detects process forks and reinitializes automatically.
    Uses lazy initialization to avoid creating resources until first use.
    """
    
    def __init__(self):
        self._owner_pid: int | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._init_lock = threading.Lock()
        os.register_at_fork(after_in_child=self._reset_after_fork)

    def _reset_after_fork(self) -> None:
        """Reset state in the child process immediately after fork."""
        self._owner_pid = None
        self._loop = None
        self._thread = None
        self._init_lock = threading.Lock()
    
    def _ensure_started(self) -> None:
        """Lazily start the dedicated event loop, reinitializing after fork if needed."""
        current_pid = os.getpid()
        
        # Fast path: already initialized for this process.
        # Snapshot self._loop into a local variable to avoid a TOCTOU race where
        # another thread could set self._loop = None between the 'is not None'
        # check and the 'is_closed()' call.
        loop = self._loop
        if self._owner_pid == current_pid and loop is not None and not loop.is_closed():
            return
        
        # Slow path: need to initialize (or reinitialize after fork)
        with self._init_lock:
            # Double-check after acquiring lock (snapshot again under the lock)
            loop = self._loop
            if self._owner_pid == current_pid and loop is not None and not loop.is_closed():
                return
            
            self._owner_pid = current_pid
            self._start_loop()
    
    def _start_loop(self) -> None:
        """Start the dedicated event loop in a background daemon thread."""
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(
            target=self._run_loop,
            daemon=True,
            name="DACS-SharedLoop"
        )
        self._thread.start()
        logger.debug(f"Shared event loop started (pid={self._owner_pid})")
    
    def _run_loop(self) -> None:
        """Run the event loop forever (executed in the background thread)."""
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()
    
    def run_coroutine(self, coro):
        """
        Run a coroutine on the shared loop and return the result (blocking).
        
        Thread-safe: can be called from any thread.
        """
        self._ensure_started()
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)
        return future.result()
    
    async def run_coroutine_async(self, coro):
        """
        Run a coroutine on the shared loop and return the result (async).
        
        Safe to call from any event loop - the coroutine runs on the shared loop.
        """
        self._ensure_started()
        future = asyncio.run_coroutine_threadsafe(coro, self._loop)
        return await asyncio.wrap_future(future)
    
    def get_loop(self) -> asyncio.AbstractEventLoop:
        """Get the shared event loop (starts it if needed)."""
        self._ensure_started()
        return self._loop


# Global shared loop manager instance
_shared_loop_manager = _SharedLoopManager()


# Helper function to get the number of CPU cores
def get_cpu_cores() -> int:
    # Note: On dacs partial node skus, os.cpu_count() does not return correct number of cpus,
    #       use DACS_CPU_CORES environment variable if set
    cpu_cores_env = os.environ.get("DACS_CPU_CORES")
    if cpu_cores_env is not None:
        try:
            cpu_cores = int(cpu_cores_env)
            if cpu_cores <= 0:
                raise ValueError("cpu cores must be > 0")
            logger.info(f"Number of CPU cores from DACS_CPU_CORES: {cpu_cores}")
            return cpu_cores
        except (ValueError, TypeError):
            logger.warning(
                f"Invalid DACS_CPU_CORES='{cpu_cores_env}', falling back to os.cpu_count()"
            )

    detected_cores = os.cpu_count()
    if detected_cores is None or detected_cores <= 0:
        logger.warning("os.cpu_count() returned invalid value; using 1 core fallback")
        return 1

    logger.info(f"DACS_CPU_CORES is not set, using os.cpu_count(): {detected_cores}")
    return int(detected_cores)


def get_memory_limit() -> int:
    # Note: use DACS_MEMORY_MB env var if set.
    #       Otherwise derive from CPU count: cores * 256 MB.
    memory_limit_env = os.environ.get("DACS_MEMORY_MB")
    if memory_limit_env is not None:
        try:
            memory_limit_mb = int(memory_limit_env)
            if memory_limit_mb <= 0:
                raise ValueError("memory limit must be > 0")
            logger.info(f"Container memory limit from DACS_MEMORY_MB: {memory_limit_mb} MB")
            return memory_limit_mb * 1024 * 1024
        except (ValueError, TypeError):
            logger.warning(
                f"Invalid DACS_MEMORY_MB='{memory_limit_env}', falling back to core-based memory limit"
            )

    cpu_cores = get_cpu_cores()
    memory_limit_mb = cpu_cores * 256
    logger.info(
        f"Container memory limit derived from CPU cores: {cpu_cores} * 256 MB = {memory_limit_mb} MB"
    )
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


_PyBytes_FromStringAndSize = ctypes.pythonapi.PyBytes_FromStringAndSize
_PyBytes_FromStringAndSize.restype = ctypes.py_object
_PyBytes_FromStringAndSize.argtypes = [c_void_p, c_ssize_t]


def safe_string_at(buffer: c_void_p, size: int) -> bytes:
    """Copy native buffer data to Python bytes with Py_ssize_t-sized lengths."""
    if size < 0:
        raise ValueError(
            f"safe_string_at: negative size {size}; native read likely returned an error code"
        )
    return _PyBytes_FromStringAndSize(buffer, size)


class BoundedBufferManager:
    """
    Thin wrapper around libc malloc/free for allocating native memory buffers.

    The acquire/release API is intentionally kept async-compatible (same signatures
    as before) so call-sites need no changes when the memory-capping logic is added.

    TODO: Implement memory capping in C++ (storageIntercept / cache_client native
    layer) and expose it via the DACSCacheClient binding so that back-pressure
    is applied without any Python-level overhead or asyncio machinery.
    """

    def __init__(self, max_memory_limit: int = 1024 * 1024 * 1024):
        # Stored for observability / future use by the C++ implementation.
        self.max_memory_limit = max_memory_limit

    # === Public thread-safe API (blocking) ===

    def acquire_buffer_sync(self, buf_size: int) -> c_void_p:
        """Allocate a buffer (blocking, safe from any thread)."""
        return alloc_buffer(buf_size)

    def release_buffer_sync(self, buffer: c_void_p) -> None:
        """Free a buffer (blocking, safe from any thread)."""
        if buffer is None:
            logger.warning("Attempted to release a None buffer. Skipping.")
            return
        release_buffer(buffer)

    # === Public async API (safe from any event loop) ===

    async def acquire_buffer(self, buf_size: int) -> c_void_p:
        """Allocate a buffer (async, safe from any event loop)."""
        return alloc_buffer(buf_size)

    async def release_buffer(self, buffer: c_void_p) -> None:
        """Free a buffer (async, safe from any event loop)."""
        if buffer is None:
            logger.warning("Attempted to release a None buffer. Skipping.")
            return
        release_buffer(buffer)

    # === Properties for observability ===

    @property
    def current_memory_usage(self) -> int:
        """Not tracked in this implementation; always returns 0."""
        return 0


async def async_parallel_ordered_result(coro_fn, items, max_concurrent: int):
    """Async generator that applies coro_fn to each item with bounded concurrency.

    Runs at most max_concurrent coroutines simultaneously and yields results in
    the same order as items, regardless of completion order.

    Usage:
        async for result in async_parallel_ordered_result(fetch, urls, max_concurrent=10):
            process(result)

    Args:
        coro_fn: Async callable taking one item and returning a result.
        items:   Iterable of inputs.
        max_concurrent: Maximum number of coroutines running at the same time.
    """
    semaphore = asyncio.Semaphore(max_concurrent)

    async def _run(item):
        async with semaphore:
            return await coro_fn(item)

    tasks = [asyncio.create_task(_run(item)) for item in items]
    for task in tasks:
        yield await task


def _initialize_dacs_client_config(
    intercept_path: str = None,
    write_mode: Optional[WriteMode] = None,
    consistent_mode: bool = False,
    config_dir: str = "/tmp",
) -> None:
    """Internal API: Initialize DACS client configuration.

    This is an internal function and should only be called from within the library.

    Args:
        intercept_path: Path to the intercept mount point
        write_mode: Write mode configuration.
        consistent_mode: Enable consistent mode (overwrite support).
        config_dir: Directory for config file (default: /tmp). Useful for testing.
    """

    # Use process-specific config path to avoid race conditions in multi-process scenarios
    # The native library reads STORAGE_INTERCEPT_CONFIG_FILEPATH env var to find the config file
    if not config_dir:
        raise ValueError("config_dir must be a non-empty path")

    os.makedirs(config_dir, exist_ok=True)
    if not os.path.isdir(config_dir):
        raise NotADirectoryError(f"config_dir is not a directory: {config_dir}")

    pid = os.getpid()
    config_path = os.path.join(config_dir, f"storageIntercept.{pid}.config")
    os.environ["STORAGE_INTERCEPT_CONFIG_FILEPATH"] = config_path

    # Check if config file already exists and warn before overwriting
    if os.path.exists(config_path):
        logger.warning(f"Config file {config_path} already exists, overwriting!!!")

    # Create the config file with owner-only (0600) permissions. It can contain
    # secrets (SAS token, access token, MSI secret), so it must not be readable
    # by other users on the host. Avoid following symlinks and apply permissions
    # to the opened file descriptor (not by path).
    if os.path.lexists(config_path) and os.path.islink(config_path):
        raise RuntimeError(f"Refusing to write config to symlink path: {config_path}")

    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    flags |= getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(config_path, flags, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w") as config_file:
        config_file.write(f"storagePath {intercept_path}\n")
        config_file.write("type blob\n")
        config_file.write("azBlobDynamicAccount true\n")
        config_file.write("azBlobDynamicContainer true\n")

        # Check if SAS token is provided (for dev operations), otherwise use managed identity
        sas_token = os.environ.get("STORAGE_INTCPT_SAS_TOKEN") if os.environ.get("USE_SAS_TOKEN", "false") == "true" else None
        access_token = os.environ.get("SI_azBlobAccessToken")
        if sas_token:
            # Strip trailing all whitespace, as it can cause issues with authentication
            sas_token = sas_token.strip()
            config_file.write(f"azBlobSASToken {sas_token}\n")
            logger.info("Using SAS token for authentication")
        elif access_token:
            config_file.write(f"azBlobAccessToken {access_token}\n")
            logger.info("Using provided access token for authentication")
        else:
            logger.info("No access or SAS token provided, falling back to token authentication")

            # Traditional MSI: use MSI endpoint + secret for the native library's internal token fetching
            # MSI_ENDPOINT and MSI_SECRET are available in cluster jobs\models and point to tokenproxy.
            azure_client_id = os.environ.get("AZURE_CLIENT_ID")
            msi_endpoint = os.environ.get("MSI_ENDPOINT")
            msi_secret = os.environ.get("MSI_SECRET")

            if azure_client_id and msi_endpoint and msi_secret:
                config_file.write(f"azBlobIdentityClientId {azure_client_id}\n")
                config_file.write(f"azBlobMSIEndpoint {msi_endpoint}\n")
                config_file.write(f"azBlobMSISecret {msi_secret}\n")

                logger.info("Using managed identity (MSI endpoint) for authentication")
            else:
                config_file.write(f"azBlobUseAzureIdentitySDK true\n")
                logger.info("Using Azure Identity SDK for authentication")

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

        # Consistent mode : supported only in write_sync and write_blob_only modes.
        if consistent_mode and write_mode in (WriteMode.WRITE_SYNC, WriteMode.WRITE_BLOB_ONLY):
            config_file.write("enableConsistentMode true\n")
            logger.info("Consistent mode enabled: overwrite support active")

    logger.debug(f"Config file initialized: {config_path}")
