# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Cache Library
# --------------------------------------------------------------------------------------------------------------------
# This library provides interfaces to perform file operations with caching mechanisms for file handles.
# It includes both basic and managed operations to handle file reads and reference counting for file handles.
# Usage:
#   First create instance of DACSCacheClient with the Storage account name and SAS token
#   Then use the instance to perform below file operations
# Exposed Functions:
#     Basic operations:
#        - get_file_size(blob_path): Get the size of a blob
#        - read_blob(blob_path, buffer, size, offset): Read data from a file with managed interfaces,
#           user need not close the file
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
from py_dacs_cache_client.dacs_cache_utils import initialize_dacs_cache_client_config, logger
from py_dacs_cache_client.dacs_cache_data_loader import DACSCacheDataLoader, Stat64, DirectoryEntry
from typing import AsyncIterator, List, Dict

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

    async def get_file_handle(self, pathname: str) -> int:
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
                filehandle_open_op = loop.run_in_executor(
                    self.executor, DACSCacheDataLoader.open, pathname, os.O_RDONLY, 0o644
                )
                self.open_operations[pathname] = filehandle_open_op

        # File handle open operation is in progress, wait for it to complete
        filehandle = await filehandle_open_op
        if filehandle == -1:
            error_message = os.strerror(ctypes.get_errno())
            logger.error(f"Failed to open file {pathname}: {error_message}")
            raise Exception(f"Failed to open file {pathname}")

        # Update the data structures with the new file handle
        async with self.lock:
            self.file_handle_map[pathname] = filehandle
            self.file_handle_counter[filehandle] += 1
            self.open_operations.pop(pathname, None)

        return filehandle

    async def release_file_handle(self, pathname: str, filehandle: int) -> None:
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
                # Not running in executor as close() as it does not involve any blocking I/O within cache client lib
                ret = DACSCacheDataLoader.close(filehandle)
                if ret < 0:
                    error_message = os.strerror(ctypes.get_errno())
                    logger.warning(f"Failed to close file handle {filehandle}: {error_message}")
            else:
                raise RuntimeError(f"File handle counter for {filehandle} is already zero or negative.")

    async def clear_file_handle_entries(self) -> None:
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
                ret = DACSCacheDataLoader.close(filehandle)
                if ret < 0:
                    error_message = os.strerror(ctypes.get_errno())
                    logger.warning(f"Failed to close file handle {filehandle}: {error_message}")
            self.file_handle_map.clear()
            self.file_handle_counter.clear()
        return


# Internal helper functions
async def _get_file_size_inner(pathname: str, executor: concurrent.futures.Executor) -> int:
    stat = Stat64()
    loop = asyncio.get_event_loop()
    ret = await loop.run_in_executor(executor, DACSCacheDataLoader.stat64, pathname, ctypes.byref(stat))
    if ret != 0:
        error_message = os.strerror(ctypes.get_errno())
        logger.error(f"Failed to get file size for {pathname}: {error_message}")
        raise Exception(f"Stat64 failed for {pathname}")
    return stat.st_size


async def _list_dir_inner(pathname: str, executor: concurrent.futures.Executor) -> List[Dict[str, object]]:
    loop = asyncio.get_event_loop()
    dirp = await loop.run_in_executor(executor, DACSCacheDataLoader.opendir, pathname)
    if not dirp:
        error_message = os.strerror(ctypes.get_errno())
        logger.error(f"Failed to open directory {pathname}: {error_message}")
        raise Exception(f"Failed to open directory {pathname}")

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
    def __init__(self, pathname: str, file_handle_manager: _FileHandleManager):
        self.pathname = pathname
        self.fd = None
        self.file_handle_manager = file_handle_manager
        self.executor = file_handle_manager.executor

    async def __aenter__(self) -> "DACSCacheFileHandle":
        self.fd = await self.file_handle_manager.get_file_handle(self.pathname)
        logger.debug(f"DACSCacheFileHandle: Got file handle {self.fd} for {self.pathname}")
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        if self.fd is not None:
            await self.file_handle_manager.release_file_handle(self.pathname, self.fd)
            logger.debug(f"DACSCacheFileHandle: Released file handle {self.fd} for {self.pathname}")

    async def read(self, buf: ctypes.c_void_p, size: int, offset: int) -> int:
        logger.debug(f"DACSCacheFileHandle: Reading {size} bytes from offset {offset} for file handle {self.fd}")
        if self.fd is None:
            raise ValueError("File descriptor is None")
        return await asyncio.get_event_loop().run_in_executor(
            self.executor, DACSCacheDataLoader.pread, ctypes.c_int(self.fd), buf, size, offset
        )


def _dacs_cache_get_intercept_file_path(
    intercept_base_path: str, storage_account_name: str, blob_path: str
) -> str:
    # Validate the blob_path and also storage_account_name used to initialize the client
    if not blob_path.startswith("az://"):
        raise ValueError(f"Invalid blob path: {blob_path}. Blob path should start with 'az://'")
    # Extract the storage account name from the blob_path and validate it
    storage_account_name_passed = blob_path.split("//")[1].split(".")[0]
    if storage_account_name != storage_account_name_passed:
        raise ValueError(f"Storage account {storage_account_name_passed} not configured, for current instance.")

    # construct the path to blob as expected by interception lib,
    #       <intercept_base_path>/<account>.blob.core.windows.net/<container>/<blob>
    # blob_path is of the form az://<account>.blob.core.windows.net/<container>/<blob>
    # so we need to remove the az:// prefix and replace it with intercept_base_path
    intercept_file_path = blob_path.replace("az://", "")
    return os.path.join(intercept_base_path, intercept_file_path)


class DACSCacheClient:
    def __init__(self, storage_account_name: str, storage_sas_token: str, executor: concurrent.futures.Executor = None):
        if executor is None:
            # Not spinning up too many threads as cache client lib internally creates its own threads
            threadpool_size = min(os.cpu_count() * 2, 24)
            executor = concurrent.futures.ThreadPoolExecutor(max_workers=threadpool_size)
            logger.info(f"No executor provided, using ThreadpoolExecutor with size {threadpool_size}")

        self.executor = executor
        self.storage_account_name = storage_account_name
        self.storage_sas_token = storage_sas_token
        self.file_handle_manager = _FileHandleManager(executor)
        self.intercept_base_path = "/mnt/dacs_cache"
        logger.debug(
            f"DACS_Cache_Client:Initializing DACSCacheClient with intercept_base_path:{self.intercept_base_path}"
        )

        # Validate storage_account_name and storage_sas_token
        if not self.storage_account_name or not self.storage_account_name.strip():
            raise ValueError("Storage account name must be provided and cannot be empty.")
        if not self.storage_sas_token or not self.storage_sas_token.strip():
            raise ValueError("Storage SAS token must be provided and cannot be empty.")

        # Configure up the dacs cache client
        initialize_dacs_cache_client_config(self.intercept_base_path, self.storage_sas_token)

        # metrics to track
        self.metrics_lock = asyncio.Lock()
        self.num_reads = 0
        self.bytes_read = 0
        self.start_time = time.time()

    async def get_file_size(self, blob_path: str) -> int:
        open_path = _dacs_cache_get_intercept_file_path(
            self.intercept_base_path, self.storage_account_name, blob_path
        )
        fileSize = await _get_file_size_inner(open_path)
        logger.debug(
            f"DACS_Cache_Client:Preloaded blob {blob_path} using interception path {open_path} and returned size:{fileSize}"
        )
        return fileSize

    async def read_blob(self, blob_path: str, buffer: ctypes.c_void_p, size: int, offset: int) -> int:
        open_path = _dacs_cache_get_intercept_file_path(
            self.intercept_base_path, self.storage_account_name, blob_path
        )
        logger.debug(
            f"DACS_Cache_Client:Reading blob {blob_path} using interception path {open_path} size:{size} offset:{offset}"
        )

        # Managed Interface to read file, not need to worry about opening file or closing the file handle
        async with DACSCacheFileHandle(open_path, self.file_handle_manager) as cacheFileHandle:
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

    async def list_dir(self, blob_path: str) -> AsyncIterator[Dict[str, object]]:
        open_path = _dacs_cache_get_intercept_file_path(
            self.intercept_base_path, self.storage_account_name, blob_path
        )
        logger.debug(f"DACS_Cache_Client:Listing directory {blob_path} using interception path {open_path}")

        # Returning a dictionary here for each entry, that gets returned as an async iterator
        # The user can parse the dictionary to get the required fields
        # d_ino, d_off, d_reclen, d_type, d_name, d_size, d_atime, d_mtime
        entries = await _list_dir_inner(open_path, self.executor)
        for entry in entries:
            yield entry

    async def shutdown(self) -> None:
        await self.file_handle_manager.clear_file_handle_entries()
        logger.debug("DACS_Cache_Client:Shutdown complete, all file handles closed")

        async with self.metrics_lock:
            end_time = time.time()
            elapsed_time = end_time - self.start_time
            logger.info(
                f"DACS_Cache_Client:Shutdown complete, "
                f"total_reads:{self.num_reads}, total_bytes_read:{self.bytes_read}, elapsed_time:{elapsed_time:.2f} seconds"
            )
