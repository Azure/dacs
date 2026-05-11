# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Client Library
# --------------------------------------------------------------------------------------------------------------------
# This library provides interfaces to perform file operations with caching mechanisms for file handles.
# It includes both basic and managed operations to handle file reads.
# All methods are safe to call concurrently - each operation uses its own file descriptor.
# Usage:
#   First create instance of DACSCacheClient (no storage account name needed as dynamic accounts are used)
#   Authentication uses either SAS token (STORAGE_INTCPT_SAS_TOKEN env var) or managed identity
#   Then use the instance to perform below file operations
# Exposed Functions:
#     Basic operations:
#        - get_file_size(blob_path): Get the size of a blob
#        - get_file_stat(blob_path): Get complete file statistics (size, mtime, atime, ctime, mode, etc.)
#        - get_file_handle(blob_path, flags): Get a managed file handle for read/write operations
#        - release_file_handle(file_handle): Release a managed file handle and close the file
#        - read_blob(blob_path, buffer, size, offset): Read data from a file with managed interfaces,
#           user need not close the file
#        - write_blob(blob_path, buffer, size): Write data to a file with managed interfaces
#        - list_dir(blob_dir_path): List all blobs in a directory
#        - shutdown(): Close all open file handles
#
# Sync Wrappers:
#     All async methods have corresponding sync versions (e.g., get_file_size_sync) that can be called
#     from any thread or event loop. These use current event loop's run_in_executor to execute the
#     core synchronous implementation without blocking.
#
# Notes:
#     - This library uses the StorageIntercept's Wrapper for file operations.
#     - Each read/write operation opens its own file descriptor for concurrent safety.
# ---------------------------------------------------------------------------------------------------------------------
import asyncio
import concurrent.futures
import ctypes
import os
import time
from py_dacs_client.dacs_utils import _initialize_dacs_client_config, logger
from py_dacs_client.dacs_data_loader import DACSCacheDataLoader, Stat64
from py_dacs_client.dacs_exceptions import check_native_result
from typing import AsyncIterator, List, Dict


# Constants
DEFAULT_INTERCEPT_PATH = "/mnt/dacs_cache"


def normalize_azure_path(path: str) -> str:
    """Normalize Azure blob path to internal DACS format.
    
    Accepts the following formats (compatible with native blobfile/boostedblob):
    - az://account/container/blob (short form - native blobfile/boostedblob format)
    - az://account.blob.core.windows.net/container/blob (long form - DACS internal format)
    - https://account.blob.core.windows.net/container/blob (https form - native blobfile format)
    
    Returns the internal DACS format:
    - az://account.blob.core.windows.net/container/blob
    
    This ensures API parity with native blobfile/boostedblob - users can use the same
    paths without modification when switching between native and DACS APIs.
    """
    if path.startswith("https://"):
        # https://account.blob.core.windows.net/container/blob
        # -> az://account.blob.core.windows.net/container/blob
        parts = path[len("https://"):].split("/", 1)
        host = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        if not host.endswith(".blob.core.windows.net"):
            raise ValueError(f"Invalid https Azure path: {path}. Expected format: https://account.blob.core.windows.net/container/blob")
        return f"az://{host}/{rest}"
    
    if path.startswith("az://"):
        # Check if it's already in long form (has .blob.core.windows.net)
        path_without_prefix = path[len("az://"):]
        parts = path_without_prefix.split("/", 1)
        first_part = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        
        if first_part.endswith(".blob.core.windows.net"):
            # Already in long form: az://account.blob.core.windows.net/container/blob
            return path
        else:
            # Short form: az://account/container/blob
            # -> az://account.blob.core.windows.net/container/blob
            account = first_part
            return f"az://{account}.blob.core.windows.net/{rest}"
    
    raise ValueError(f"Invalid URL '{path}'; expected 'https' or 'az' scheme")


# Internal helper functions to handle errno properly
def _stat64_with_errno(pathname: str, stat_ptr) -> tuple:
    """Wrapper to capture errno along with stat64 return value."""
    ret = DACSCacheDataLoader.stat64(pathname, stat_ptr)
    errno_val = ctypes.get_errno() if ret != 0 else 0
    return (ret, errno_val)


def _open_with_errno(pathname: str, flags: int, mode: int) -> tuple:
    """Wrapper to capture errno along with open return value."""
    fd = DACSCacheDataLoader.open(pathname, flags, mode)
    errno_val = ctypes.get_errno() if fd == -1 else 0
    return (fd, errno_val)


def _opendir_with_errno(pathname: str) -> tuple:
    """Wrapper to capture errno along with opendir return value."""
    dirp = DACSCacheDataLoader.opendir(pathname)
    errno_val = ctypes.get_errno() if not dirp else 0
    return (dirp, errno_val)


def _get_file_stat_inner_sync(pathname: str) -> Stat64:
    """Synchronous core implementation of file stat."""
    stat = Stat64()
    ret, errno_val = _stat64_with_errno(pathname, ctypes.byref(stat))
    if ret != 0:
        logger.error(f"Failed to get file stat for {pathname}: {os.strerror(errno_val)}, errno={errno_val}")
    check_native_result(ret, operation="stat", path=pathname)
    return stat


def _list_dir_inner_sync(pathname: str) -> List[Dict[str, object]]:
    """Synchronous core implementation of directory listing."""
    dirp, errno_val = _opendir_with_errno(pathname)
    if not dirp:
        logger.error(f"Failed to open directory {pathname}: {os.strerror(errno_val)}, errno={errno_val}")
        check_native_result(-1, operation="opendir", path=pathname)

    logger.debug(f"Listing directory {pathname} with dirp {dirp}")
    entries = []
    while True:
        dirent = DACSCacheDataLoader.readdir(dirp)
        if not dirent or not dirent.contents:
            break

        # Note: d_version is intentionally excluded — it is an internal field
        entries.append(
            {
                "d_ino": dirent.contents.d_ino,
                "d_off": dirent.contents.d_off,
                "d_reclen": dirent.contents.d_reclen,
                "d_type": dirent.contents.d_type,
                "d_name": dirent.contents.d_name.decode("utf-8"),
                "d_size": dirent.contents.d_size,
                "d_atime": dirent.contents.d_atime,
                "d_mtime": dirent.contents.d_mtime,
            }
        )

    # Close the directory handle, ignore errors but log them
    ret = DACSCacheDataLoader.closedir(dirp)
    if ret != 0:
        error_message = os.strerror(ctypes.get_errno())
        logger.error(f"Failed to close directory {pathname}: {error_message}")
    return entries


# Managed file operations with context manager
class DACSCacheFileHandle:
    """
    Managed file handle for DACS cache operations.
    
    Supports both sync and async usage:
    - Sync: Use open_sync()/close_sync()/read_sync()/write_sync() directly
    - Async: Use as async context manager with read()/write() methods
    
    The sync methods are the core implementation; async methods wrap them
    via run_in_executor for non-blocking operation.
    """
    
    def __init__(self, pathname: str, flags: int, executor: concurrent.futures.Executor = None):
        self.pathname = pathname
        self.flags = flags
        self.fd = None
        self.executor = executor

    # =========================================================================
    # Sync Methods (Core Implementation)
    # =========================================================================

    def open_sync(self) -> "DACSCacheFileHandle":
        """Open the file synchronously. Returns self for chaining."""
        fd, errno_val = _open_with_errno(self.pathname, self.flags, 0o644)
        if fd == -1:
            logger.error(f"Failed to open file {self.pathname}: {os.strerror(errno_val)}")
        check_native_result(fd, operation="open", path=self.pathname)
        self.fd = fd
        logger.debug(f"DACSCacheFileHandle: Opened file handle {self.fd} for {self.pathname}")
        return self

    def close_sync(self) -> None:
        """Close the file synchronously."""
        if self.fd is not None:
            ret = DACSCacheDataLoader.close(self.fd)
            if ret < 0:
                logger.error(f"Failed to close file handle {self.fd}: {os.strerror(ctypes.get_errno())}")
            check_native_result(ret, operation="close", path=self.pathname)
            logger.debug(f"DACSCacheFileHandle: Closed file handle {self.fd} for {self.pathname}")
            self.fd = None

    def read_sync(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        """Read from file synchronously."""
        logger.debug(f"DACSCacheFileHandle: Reading {size} bytes from offset {offset} for file handle {self.fd}")
        if self.fd is None:
            raise ValueError("File descriptor is None")

        result = DACSCacheDataLoader.pread(ctypes.c_int(self.fd), buf, size, offset)
        return check_native_result(result, operation="read", path=self.pathname)

    def write_sync(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        """Write to file synchronously."""
        logger.debug(f"DACSCacheFileHandle: Writing {size} bytes to offset {offset} for file handle {self.fd}")
        if self.fd is None:
            raise ValueError("File descriptor is None")
        result = DACSCacheDataLoader.pwrite(ctypes.c_int(self.fd), buf, size, offset)
        return check_native_result(result, operation="write", path=self.pathname)

    # Sync context manager support
    def __enter__(self) -> "DACSCacheFileHandle":
        return self.open_sync()

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close_sync()

    # =========================================================================
    # Async Methods (Wrappers around sync methods)
    # =========================================================================

    async def __aenter__(self) -> "DACSCacheFileHandle":
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(self.executor, self.open_sync)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(self.executor, self.close_sync)

    async def read(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        """Async version of read - runs sync method in executor."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.read_sync, buf, size, offset)

    async def write(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        """Async version of write - runs sync method in executor."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.write_sync, buf, size, offset)


def _dacs_get_intercept_file_path(intercept_base_path: str, blob_path: str) -> str:
    # Normalize the path to internal format (supports both native and DACS path formats)
    normalized_path = normalize_azure_path(blob_path)
    
    # construct the path to blob as expected by interception lib,
    #       <intercept_base_path>/<account>.blob.core.windows.net/<container>/<blob>
    # normalized_path is of the form az://<account>.blob.core.windows.net/<container>/<blob>
    # so we need to remove the az:// prefix and replace it with intercept_base_path
    intercept_file_path = normalized_path.replace("az://", "")
    return os.path.join(intercept_base_path, intercept_file_path)


class DACSCacheClient:
    """
    Client for DACS cache operations with both sync and async APIs.
    
    Architecture:
        - Sync methods (*_sync) are the core implementation with direct blocking calls
        - Async methods wrap sync methods via run_in_executor for non-blocking operation
        - This eliminates event loop overhead for sync callers while preserving async APIs
    
    Thread Safety:
        All methods are safe to call concurrently - each operation uses its own file descriptor.
    """
    
    def __init__(self, executor: concurrent.futures.Executor = None, write_mode=None):
        if executor is None:
            self.reset_executor()
        else:
            self.executor = executor

        self.intercept_base_path = DEFAULT_INTERCEPT_PATH
        self.write_mode = write_mode
        logger.debug(f"DACS_Cache_Client:Initializing DACSCacheClient, write_mode:{write_mode}")

        # Configure up the DACS client
        _initialize_dacs_client_config(intercept_path=self.intercept_base_path, write_mode=write_mode)

        # Metrics to track (no lock needed - see explanation below)
        # These counters are updated without synchronization because:
        # 1. CPython's GIL ensures integer += operations won't corrupt memory
        # 2. Worst case is a lost update (counter shows N+1 instead of N+2 under race)
        # 3. These are diagnostic metrics for observability, not critical for correctness
        # 4. Avoiding asyncio.Lock allows cross-event-loop usage and removes overhead
        self.num_reads = 0
        self.num_writes = 0
        self.bytes_read = 0
        self.bytes_written = 0
        self.start_time = time.time()

    def set_executor(self, executor: concurrent.futures.Executor) -> None:
        """
        Set a custom executor for running blocking I/O operations.

        Args:
            executor: A concurrent.futures.Executor (e.g., ThreadPoolExecutor, ProcessPoolExecutor)
        """
        if executor is None:
            raise ValueError("executor cannot be None, use reset_executor() to restore default")
        self.executor = executor
        logger.info(f"Custom executor set: {type(executor).__name__}")

    def get_executor(self) -> concurrent.futures.Executor:
        """Get the current executor being used for blocking operations."""
        return self.executor

    def reset_executor(self) -> None:
        """
        Reset to the default ThreadPoolExecutor.

        Creates a new ThreadPoolExecutor with size depending on cpu count
        """
        cpu_count = os.cpu_count() or 8  # Fallback to 8 if os.cpu_count() returns None
        threadpool_size = max(4, min(cpu_count * 2, 40))
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=threadpool_size)
        logger.info(f"Executor reset to default ThreadPoolExecutor with size {threadpool_size}")

    # =========================================================================
    # Sync Methods (Core Implementation)
    # =========================================================================

    def get_file_size_sync(self, blob_path: str) -> int:
        """
        Get the size of a blob file (synchronous).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)

        Returns:
            int: File size in bytes

        Raises:
            DACSStorageError: If stat operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        stat = _get_file_stat_inner_sync(open_path)
        fileSize = stat.st_size

        logger.debug(
            f"DACS_Cache_Client:Preloaded blob {blob_path} using interception path {open_path} and returned size:{fileSize}"
        )
        return fileSize

    def get_file_stat_sync(self, blob_path: str) -> Stat64:
        """
        Get complete file statistics for a blob (synchronous).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)

        Returns:
            Stat64: Complete file statistics including size, mtime, atime, ctime, mode, etc.

        Raises:
            DACSStorageError: If stat operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        stat = _get_file_stat_inner_sync(open_path)

        logger.debug(
            f"DACS_Cache_Client:Retrieved file stat for blob {blob_path} using interception path {open_path}"
        )
        return stat

    def get_file_handle_sync(self, blob_path: str, flags: int) -> DACSCacheFileHandle:
        """
        Get a managed file handle for a blob (synchronous).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            flags: File open flags (e.g., os.O_RDONLY, os.O_WRONLY, os.O_CREAT)

        Returns:
            DACSCacheFileHandle: Managed file handle that can be used for read/write operations

        Raises:
            DACSStorageError: If file open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        file_handle = DACSCacheFileHandle(open_path, flags, self.executor).open_sync()
        logger.debug(
            f"DACS_Cache_Client:Got file handle {file_handle} for blob {blob_path} using interception path {open_path}"
        )
        return file_handle

    def release_file_handle_sync(self, file_handle: DACSCacheFileHandle) -> None:
        """
        Release a managed file handle and close the underlying file (synchronous).

        Args:
            file_handle: DACSCacheFileHandle instance to release

        Raises:
            DACSStorageError: If file close operation fails
        """
        file_handle.close_sync()
        logger.debug(f"DACS_Cache_Client:Released file handle for {file_handle.pathname}")

    def read_blob_sync(self, blob_path: str, buffer: ctypes.c_void_p, size: int, offset: int) -> int:
        """
        Read data from a blob into a buffer (synchronous).

        Uses managed file handle internally - file opening and closing is handled automatically.
        Updates read metrics (num_reads, bytes_read).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            buffer: C buffer to read data into (allocated with alloc_buffer or ctypes array)
            size: Number of bytes to read
            offset: Offset in the file to start reading from

        Returns:
            int: Number of bytes actually read

        Raises:
            IOError: If read size doesn't match requested size
            DACSStorageError: If file open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(
            f"DACS_Cache_Client:Reading blob {blob_path} using interception path {open_path} size:{size} offset:{offset}"
        )

        # Managed Interface to read file using sync context manager
        with DACSCacheFileHandle(open_path, os.O_RDONLY, self.executor) as cacheFileHandle:
            read_size = cacheFileHandle.read_sync(buffer, size, offset)
            if read_size != size:
                logger.error(f"Expected to read {size} bytes but only read {read_size} bytes from offset:{offset}")
                raise IOError(f"Incomplete read: expected {size} bytes but got {read_size} bytes")
        logger.debug(
            f"DACS_Cache_Client:managed_read_inner - Successfully read blob with size:{read_size} from offset:{offset}"
        )

        # Update metrics (no lock - GIL provides sufficient safety for diagnostic counters)
        self.num_reads += 1
        self.bytes_read += read_size
        return read_size

    def write_blob_sync(self, blob_path: str, buffer: ctypes.c_void_p, size: int) -> int:
        """
        Write data from a buffer to a blob (synchronous).

        Uses managed file handle internally - file opening and closing is handled automatically.
        Creates the file if it doesn't exist (O_CREAT flag). Uses O_EXCL to fail if exists.
        Updates write metrics (num_writes, bytes_written).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            buffer: C buffer containing data to write (allocated with alloc_buffer or ctypes array)
            size: Number of bytes to write

        Returns:
            int: Number of bytes actually written

        Raises:
            IOError: If write size doesn't match requested size
            DACSStorageError: If file open/create operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(f"DACS_Cache_Client:Writing blob {blob_path} using interception path {open_path} size:{size}")

        # Using sync context manager
        with DACSCacheFileHandle(open_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, self.executor) as cacheFileHandle:
            write_size = cacheFileHandle.write_sync(buffer, size, 0)
            if write_size != size:
                logger.error(f"Expected to write {size} bytes but only wrote {write_size} bytes")
                raise IOError(f"Incomplete write: expected {size} bytes but wrote {write_size} bytes")
        logger.debug(
            f"DACS_Cache_Client:managed_write_inner - Successfully wrote blob with size:{write_size} to offset:0"
        )

        # Update metrics (no lock - GIL provides sufficient safety for diagnostic counters)
        self.num_writes += 1
        self.bytes_written += write_size
        return write_size

    def list_dir_sync(self, blob_path: str) -> List[Dict[str, object]]:
        """
        List all entries in a blob directory (synchronous).

        Args:
            blob_path: Azure blob directory path (e.g., az://account/container/dir/)

        Returns:
            List[Dict[str, object]]: List of dictionaries for each entry with fields:
                - d_ino: Inode number
                - d_off: Offset to next entry
                - d_reclen: Length of this record
                - d_type: File type
                - d_name: Entry name (str)
                - d_size: File size
                - d_atime: Last access time
                - d_mtime: Last modification time

        Raises:
            DACSStorageError: If directory open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(f"DACS_Cache_Client:Listing directory {blob_path} using interception path {open_path}")
        return _list_dir_inner_sync(open_path)

    def shutdown_sync(self) -> None:
        """
        Shutdown the DACS Client and clean up resources (synchronous).

        Logs final metrics including:
        - Total reads and bytes read
        - Total writes and bytes written
        - Total elapsed time since client initialization

        This should be called before terminating the application.
        """
        logger.debug("DACS_Cache_Client:Shutdown complete")

        end_time = time.time()
        elapsed_time = end_time - self.start_time
        logger.info(
            f"DACS_Cache_Client:Shutdown complete, "
            f"total_reads:{self.num_reads}, total_bytes_read:{self.bytes_read}, elapsed_time:{elapsed_time:.2f} seconds, "
            f"total_writes:{self.num_writes}, total_bytes_written:{self.bytes_written}"
        )

    # =========================================================================
    # Async Methods (Wrappers around sync methods via run_in_executor)
    # =========================================================================

    async def get_file_size(self, blob_path: str) -> int:
        """Async version of get_file_size_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.get_file_size_sync, blob_path)

    async def get_file_stat(self, blob_path: str) -> Stat64:
        """Async version of get_file_stat_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.get_file_stat_sync, blob_path)

    async def get_file_handle(self, blob_path: str, flags: int) -> DACSCacheFileHandle:
        """Async version of get_file_handle_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.get_file_handle_sync, blob_path, flags)

    async def release_file_handle(self, file_handle: DACSCacheFileHandle) -> None:
        """Async version of release_file_handle_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.release_file_handle_sync, file_handle)

    async def read_blob(self, blob_path: str, buffer: ctypes.c_void_p, size: int, offset: int) -> int:
        """Async version of read_blob_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.read_blob_sync, blob_path, buffer, size, offset)

    async def write_blob(self, blob_path: str, buffer: ctypes.c_void_p, size: int) -> int:
        """Async version of write_blob_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.write_blob_sync, blob_path, buffer, size)

    async def list_dir(self, blob_path: str) -> AsyncIterator[Dict[str, object]]:
        """
        Async version of list_dir_sync() - yields entries as an async iterator.
        
        Note: The actual I/O is done synchronously in the executor, but results
        are yielded asynchronously for API compatibility.
        """
        loop = asyncio.get_running_loop()
        entries = await loop.run_in_executor(self.executor, self.list_dir_sync, blob_path)
        for entry in entries:
            yield entry

    async def shutdown(self) -> None:
        """Async version of shutdown_sync() - runs in executor for non-blocking operation."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(self.executor, self.shutdown_sync)
