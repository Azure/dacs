# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Client Library
# --------------------------------------------------------------------------------------------------------------------
# This library provides interfaces to perform file operations with caching mechanisms for file handles.
# It includes both basic and managed operations to handle file reads and reference counting for file handles.
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
# Notes:
#     - This library uses the StorageIntercept's Wrapper for file operations.
# ---------------------------------------------------------------------------------------------------------------------
import asyncio
import concurrent.futures
import ctypes
import os
import sys
import time
from collections import defaultdict
from py_dacs_client.dacs_utils import _initialize_dacs_client_config, logger
from py_dacs_client.dacs_data_loader import DACSCacheDataLoader, Stat64, DirectoryEntry
from py_dacs_client.dacs_exceptions import (
    DACSError,
    DACSStorageError,
    handle_native_error,
    check_native_result,
)
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


# Internal class to manage file handles
class _FileHandleManager:
    def __init__(self, executor: concurrent.futures.Executor):
        # Executor for running blocking I/O operations
        self.executor = executor
        # Map to store file handles
        self.file_handle_map = defaultdict(lambda: None)
        # Map to store in-progress open operations
        self.open_operations = {}
        # Reference counting for file handles
        self.file_handle_counter = defaultdict(int)
        # Using asyncio.Lock to ensure thread safety
        self.lock = asyncio.Lock()

    async def get_file_handle(self, pathname: str, flags: int) -> int:
        # Check if the file handle is already opened or in-progress
        filehandle = None
        filehandle_open_op = None
        async with self.lock:
            filehandle = self.file_handle_map.get(pathname)
            if filehandle is not None:
                # File handle already exists, increment the reference count and return
                self.file_handle_counter[filehandle] += 1
                return filehandle
            # file handle is not found, check if an open operation is in progress
            filehandle_open_op = self.open_operations.get(pathname)
            if not filehandle_open_op:
                loop = asyncio.get_event_loop()
                filehandle_open_op = loop.run_in_executor(self.executor, _open_with_errno, pathname, flags, 0o644)
                self.open_operations[pathname] = filehandle_open_op

        # File handle open operation is in progress, wait for it to complete
        filehandle, errno_val = await filehandle_open_op
        if filehandle == -1:
            error_message = os.strerror(errno_val)
            logger.error(f"Failed to open file {pathname}: {error_message}")
            raise OSError(errno_val, f"Failed to open file: {error_message}", pathname)

        # Update the data structures with the new file handle
        async with self.lock:
            self.file_handle_map[pathname] = filehandle
            self.file_handle_counter[filehandle] += 1
            self.open_operations.pop(pathname, None)

        return filehandle

    async def release_file_handle(self, pathname: str, filehandle: int) -> None:
        close_op = None
        async with self.lock:
            # Release file handle reference
            # If one of instances of DACSCacheFileHandle for the same file goes off,
            # the other instances should still be able to operate on the file.
            # Only close the file if all instances are closed
            if self.file_handle_counter[filehandle] > 1:
                self.file_handle_counter[filehandle] -= 1
            elif self.file_handle_counter[filehandle] == 1:
                self.file_handle_counter.pop(filehandle, None)
                self.file_handle_map.pop(pathname, None)

                # Close may take time due for write flushes, so do not hold the lock during close
                loop = asyncio.get_event_loop()
                close_op = loop.run_in_executor(self.executor, DACSCacheDataLoader.close, filehandle)
            else:
                raise RuntimeError(f"File handle counter for {filehandle} is already zero or negative.")

        if close_op:
            ret = await close_op
            if ret < 0:
                error_message = os.strerror(ctypes.get_errno())
                logger.error(f"Failed to close file handle {filehandle}: {error_message}")
                raise OSError(ctypes.get_errno(), f"Failed to close file handle: {error_message}")
        return

    async def clear_file_handle_entries(self) -> None:
        close_ops = []
        async with self.lock:
            for pathname in list(self.file_handle_map.keys()):
                filehandle = self.file_handle_map[pathname]
                if filehandle is None:
                    logger.error(f"File handle not found for pathname {pathname}, errno: {ctypes.get_errno()}")
                    continue
                # Release the file handles
                self.file_handle_counter.pop(filehandle, None)
                self.file_handle_map.pop(pathname, None)
                # Close the file handle irrespective of the reference count, ignore errors
                close_ops.append(
                    asyncio.get_event_loop().run_in_executor(self.executor, DACSCacheDataLoader.close, filehandle)
                )
            self.file_handle_map.clear()
            self.file_handle_counter.clear()

        # Wait for all close operations to complete
        for close_op in close_ops:
            ret = await close_op
            if ret < 0:
                error_message = os.strerror(ctypes.get_errno())
                logger.error(f"Failed to close file handle during clear: {error_message}")
        return


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


async def _get_file_size_inner(pathname: str, executor: concurrent.futures.Executor) -> int:
    stat = Stat64()
    loop = asyncio.get_event_loop()
    ret, errno_val = await loop.run_in_executor(executor, _stat64_with_errno, pathname, ctypes.byref(stat))
    if ret != 0:
        error_message = os.strerror(errno_val)
        logger.error(f"Failed to get file size for {pathname}: {error_message}, errno={errno_val}")
        raise OSError(errno_val, error_message, pathname)
    return stat.st_size


async def _get_file_stat_inner(pathname: str, executor: concurrent.futures.Executor) -> Stat64:
    stat = Stat64()
    loop = asyncio.get_event_loop()
    ret, errno_val = await loop.run_in_executor(executor, _stat64_with_errno, pathname, ctypes.byref(stat))
    if ret != 0:
        error_message = os.strerror(errno_val)
        logger.error(f"Failed to get file stat for {pathname}: {error_message}, errno={errno_val}")
        raise OSError(errno_val, error_message, pathname)
    return stat


async def _list_dir_inner(pathname: str, executor: concurrent.futures.Executor) -> List[Dict[str, object]]:
    loop = asyncio.get_event_loop()
    dirp, errno_val = await loop.run_in_executor(executor, _opendir_with_errno, pathname)
    if not dirp:
        error_message = os.strerror(errno_val)
        logger.error(f"Failed to open directory {pathname}: {error_message}, errno={errno_val}")
        raise OSError(errno_val, f"Failed to open directory: {error_message}", pathname)

    logger.debug(f"Listing directory {pathname} with dirp {dirp}")
    entries = []
    while True:
        dirent = await loop.run_in_executor(executor, DACSCacheDataLoader.readdir, dirp)
        if not dirent or not dirent.contents:
            break

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

    # Close the directory handle
    # Not running closedir in executor as it does not involve any blocking I/O within cache client lib
    ret = DACSCacheDataLoader.closedir(dirp)
    if ret != 0:
        error_message = os.strerror(ctypes.get_errno())
        logger.error(f"Failed to close directory {pathname}: {error_message}")
    return entries


# Managed file operations with context manager
class DACSCacheFileHandle:
    def __init__(self, pathname: str, flags: int, file_handle_manager: _FileHandleManager):
        self.pathname = pathname
        self.flags = flags
        self.fd = None
        self.file_handle_manager = file_handle_manager
        self.executor = file_handle_manager.executor

    async def __aenter__(self) -> "DACSCacheFileHandle":
        try:
            self.fd = await self.file_handle_manager.get_file_handle(self.pathname, self.flags)
            logger.debug(f"DACSCacheFileHandle: Got file handle {self.fd} for {self.pathname}")
            return self
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="open", path=self.pathname) from e

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        if self.fd is not None:
            try:
                await self.file_handle_manager.release_file_handle(self.pathname, self.fd)
                logger.debug(f"DACSCacheFileHandle: Released file handle {self.fd} for {self.pathname}")
            except Exception as e:
                logger.warning(f"Error releasing file handle {self.fd} for {self.pathname}: {e}")
                raise handle_native_error(e, operation="close", path=self.pathname) from e
            finally:
                self.fd = None

    async def read(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        logger.debug(f"DACSCacheFileHandle: Reading {size} bytes from offset {offset} for file handle {self.fd}")
        if self.fd is None:
            raise ValueError("File descriptor is None")
        try:
            result = await asyncio.get_event_loop().run_in_executor(
                self.executor, DACSCacheDataLoader.pread, ctypes.c_int(self.fd), buf, size, offset
            )
            return check_native_result(result, operation="read", path=self.pathname)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="read", path=self.pathname) from e

    async def write(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        logger.debug(f"DACSCacheFileHandle: Writing {size} bytes to offset {offset} for file handle {self.fd}")
        if self.fd is None:
            raise ValueError("File descriptor is None")
        try:
            result = await asyncio.get_event_loop().run_in_executor(
                self.executor, DACSCacheDataLoader.pwrite, ctypes.c_int(self.fd), buf, size, offset
            )
            return check_native_result(result, operation="write", path=self.pathname)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="write", path=self.pathname) from e


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
    def __init__(self, executor: concurrent.futures.Executor = None, write_mode=None):
        if executor is None:
            # Not spinning up too many threads as cache client lib internally creates its own threads
            threadpool_size = min(os.cpu_count() * 2, 24)
            executor = concurrent.futures.ThreadPoolExecutor(max_workers=threadpool_size)
            logger.info(f"No executor provided, using ThreadpoolExecutor with size {threadpool_size}")

        self.executor = executor
        self.file_handle_manager = _FileHandleManager(executor)
        self.intercept_base_path = DEFAULT_INTERCEPT_PATH
        self.write_mode = write_mode
        logger.debug(f"DACS_Cache_Client:Initializing DACSCacheClient, write_mode:{write_mode}")

        # Configure up the DACS client
        _initialize_dacs_client_config(intercept_path=self.intercept_base_path, write_mode=write_mode)

        # metrics to track
        self.metrics_lock = asyncio.Lock()
        self.num_reads = 0
        self.num_writes = 0
        self.bytes_read = 0
        self.bytes_written = 0
        self.start_time = time.time()

    async def get_file_size(self, blob_path: str) -> int:
        """
        Get the size of a blob file.

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)

        Returns:
            int: File size in bytes

        Raises:
            Exception: If stat operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        stat = await _get_file_stat_inner(open_path, self.executor)
        fileSize = stat.st_size

        logger.debug(
            f"DACS_Cache_Client:Preloaded blob {blob_path} using interception path {open_path} and returned size:{fileSize}"
        )
        return fileSize

    async def get_file_stat(self, blob_path: str) -> Stat64:
        """
        Get complete file statistics for a blob.

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)

        Returns:
            Stat64: Complete file statistics including size, mtime, atime, ctime, mode, etc.
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        stat = await _get_file_stat_inner(open_path, self.executor)

        logger.debug(
            f"DACS_Cache_Client:Retrieved file stat for blob {blob_path} using interception path {open_path}"
        )
        return stat

    async def get_file_handle(self, blob_path: str, flags: int) -> DACSCacheFileHandle:
        """
        Get a managed file handle for a blob.

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            flags: File open flags (e.g., os.O_RDONLY, os.O_WRONLY, os.O_CREAT)

        Returns:
            DACSCacheFileHandle: Managed file handle that can be used for read/write operations

        Raises:
            Exception: If file open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        file_handle = await DACSCacheFileHandle(open_path, flags, self.file_handle_manager).__aenter__()
        logger.debug(
            f"DACS_Cache_Client:Got file handle {file_handle} for blob {blob_path} using interception path {open_path}"
        )
        return file_handle

    async def release_file_handle(self, file_handle: DACSCacheFileHandle) -> None:
        """
        Release a managed file handle and close the underlying file.

        This calls the async exit handler to properly decrement reference counts
        and close the file if no other references exist.

        Args:
            file_handle: DACSCacheFileHandle instance to release

        Raises:
            Exception: If file close operation fails
        """
        await file_handle.__aexit__(None, None, None)
        logger.debug(f"DACS_Cache_Client:Released file handle for {file_handle.pathname}")

    async def read_blob(self, blob_path: str, buffer: ctypes.c_void_p, size: int, offset: int) -> int:
        """
        Read data from a blob into a buffer.

        Uses managed file handle internally - file opening and closing is handled automatically.
        Updates read metrics (num_reads, bytes_read).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            buffer: C buffer to read data into (allocated with alloc_buffer)
            size: Number of bytes to read
            offset: Offset in the file to start reading from

        Returns:
            int: Number of bytes actually read

        Raises:
            IOError: If read size doesn't match requested size
            Exception: If file open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(
            f"DACS_Cache_Client:Reading blob {blob_path} using interception path {open_path} size:{size} offset:{offset}"
        )

        # Managed Interface to read file, not need to worry about opening file or closing the file handle
        async with DACSCacheFileHandle(open_path, os.O_RDONLY, self.file_handle_manager) as cacheFileHandle:
            read_size = await cacheFileHandle.read(buffer, size, offset)
            if read_size != size:
                logger.error(f"Expected to read {size} bytes but only read {read_size} bytes from offset:{offset}")
                raise IOError(f"Incomplete read: expected {size} bytes but got {read_size} bytes")
        logger.debug(
            f"DACS_Cache_Client:managed_read_inner - Successfully read blob with size:{read_size} from offset:{offset}"
        )

        async with self.metrics_lock:
            self.num_reads += 1
            self.bytes_read += read_size
        return read_size

    async def write_blob(self, blob_path: str, buffer: ctypes.c_void_p, size: int) -> int:
        """
        Write data from a buffer to a blob.

        Uses managed file handle internally - file opening and closing is handled automatically.
        Creates the file if it doesn't exist (O_CREAT flag). Uses O_TRUNC to overwrite if exists.
        Updates write metrics (num_writes, bytes_written).

        Args:
            blob_path: Azure blob path (e.g., az://account/container/path)
            buffer: C buffer containing data to write (allocated with alloc_buffer)
            size: Number of bytes to write

        Returns:
            int: Number of bytes actually written

        Raises:
            IOError: If write size doesn't match requested size
            Exception: If file open/create operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(f"DACS_Cache_Client:Writing blob {blob_path} using interception path {open_path} size:{size}")

        # Using Managed Interface to write file, not need to worry about opening file or closing the file handle
        async with DACSCacheFileHandle(
            open_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, self.file_handle_manager
        ) as cacheFileHandle:
            write_size = await cacheFileHandle.write(buffer, size, 0)
            if write_size != size:
                logger.error(f"Expected to write {size} bytes but only wrote {write_size} bytes")
                raise IOError(f"Incomplete write: expected {size} bytes but wrote {write_size} bytes")
        logger.debug(
            f"DACS_Cache_Client:managed_write_inner - Successfully wrote blob with size:{write_size} to offset:0"
        )

        async with self.metrics_lock:
            self.num_writes += 1
            self.bytes_written += write_size
        return write_size

    async def list_dir(self, blob_path: str) -> AsyncIterator[Dict[str, object]]:
        """
        List all entries in a blob directory.

        Args:
            blob_path: Azure blob directory path (e.g., az://account/container/dir/)

        Yields:
            Dict[str, object]: Dictionary for each entry with fields:
                - d_ino: Inode number
                - d_off: Offset to next entry
                - d_reclen: Length of this record
                - d_type: File type
                - d_name: Entry name (str)
                - d_size: File size
                - d_atime: Last access time
                - d_mtime: Last modification time

        Raises:
            Exception: If directory open operation fails
        """
        open_path = _dacs_get_intercept_file_path(self.intercept_base_path, blob_path)
        logger.debug(f"DACS_Cache_Client:Listing directory {blob_path} using interception path {open_path}")

        # Returning a dictionary here for each entry, that gets returned as an async iterator
        # The user can parse the dictionary to get the required fields
        # d_ino, d_off, d_reclen, d_type, d_name, d_size, d_atime, d_mtime
        entries = await _list_dir_inner(open_path, self.executor)
        for entry in entries:
            yield entry

    async def shutdown(self) -> None:
        """
        Shutdown the DACS Client and clean up resources.

        Closes all open file handles and logs final metrics including:
        - Total reads and bytes read
        - Total writes and bytes written
        - Total elapsed time since client initialization

        This should be called before terminating the application to ensure
        proper cleanup of file handles and resources.
        """
        await self.file_handle_manager.clear_file_handle_entries()
        logger.debug("DACS_Cache_Client:Shutdown complete, all file handles closed")

        async with self.metrics_lock:
            end_time = time.time()
            elapsed_time = end_time - self.start_time
            logger.info(
                f"DACS_Cache_Client:Shutdown complete, "
                f"total_reads:{self.num_reads}, total_bytes_read:{self.bytes_read}, elapsed_time:{elapsed_time:.2f} seconds"
                f"total_writes:{self.num_writes}, total_bytes_written:{self.bytes_written}"
            )
