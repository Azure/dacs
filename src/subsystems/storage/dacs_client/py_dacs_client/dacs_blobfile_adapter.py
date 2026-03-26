#!/usr/bin/env python3
# --------------------------------------------------------------------------------------------------------------------
# DACS Client - Blobfile API
# Provides blobfile-compatible API for dacs_client
# --------------------------------------------------------------------------------------------------------------------

import asyncio
import ctypes
import errno
import hashlib
import logging
import os
import shutil
import stat as stat_module
import tempfile
import threading
import time
from collections import namedtuple
from typing import Optional, Union, Iterator, List, Any, Tuple
from enum import Enum

from py_dacs_client.dacs_client import DACSCacheClient, normalize_azure_path
from py_dacs_client.dacs_utils import alloc_buffer, release_buffer, BoundedBufferManager, get_memory_limit
from py_dacs_client.dacs_exceptions import (
    DACSError,
    DACSStorageError,
    DACSAuthenticationError,
    DACSPermissionError,
    DACSNotFoundError,
    DACSExistsError,
    DACSBusyError,
    DACSTimeoutError,
    DACSNetworkError,
    DACSQuotaError,
    DACSInvalidInputError,
    DACSServiceUnavailableError,
    DACSIsDirectoryError,
    DACSNotDirectoryError,
    DACSNotEmptyError,
    DACSNameTooLongError,
    DACSReadOnlyError,
    OperationNotSupportedError,
    handle_native_error,
    check_native_result,
    errno_to_exception_class,
)

# Re-export exceptions for backwards compatibility
__all__ = [
    "DACSBlobfileAdapter",
    "BlobFile",  # Alias for drop-in blobfile compatibility
    "WriteMode",
    "DACSError",
    "DACSStorageError",
    "DACSAuthenticationError",
    "DACSPermissionError",
    "DACSNotFoundError",
    "DACSExistsError",
    "DACSBusyError",
    "DACSTimeoutError",
    "DACSNetworkError",
    "DACSQuotaError",
    "DACSInvalidInputError",
    "DACSServiceUnavailableError",
    "DACSIsDirectoryError",
    "DACSNotDirectoryError",
    "DACSNotEmptyError",
    "DACSNameTooLongError",
    "DACSReadOnlyError",
    "OperationNotSupportedError",
]

logger = logging.getLogger(__name__)

def _is_azure_path(path: str) -> bool:
    """Check if a path is an Azure blob path (az:// or https://<account>.blob.core.windows.net/<container>/<blob>)."""
    return (
        path.startswith("az://")
        or (path.startswith("https://") and ".blob.core.windows.net" in path)
    )

class _ClientManager:
    """Thread-safe singleton manager for DACSCacheClient and BoundedBufferManager instances."""

    _instance = None
    _buffer_manager = None
    _lock = threading.Lock()
    _write_mode = None

    @classmethod
    def get_instance(cls, write_mode: Optional["WriteMode"] = None) -> DACSCacheClient:
        """
        Get or create the singleton DACSCacheClient instance.
        Thread-safe lazy initialization using double-checked locking pattern.

        Args:
            write_mode: Write mode configuration for the client

        Returns:
            DACSCacheClient: The singleton instance
        """
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._write_mode = write_mode if write_mode else WriteMode.WRITE_SYNC
                    cls._instance = DACSCacheClient(write_mode=cls._write_mode)
        return cls._instance

    @classmethod
    def get_buffer_manager(cls) -> BoundedBufferManager:
        """
        Get or create the singleton BoundedBufferManager instance.
        The memory limit is derived from the container memory limit
        (DACS_MEMORY_MB env var, default 2 GB), using 80% of
        that limit for buffer allocations.

        Returns:
            BoundedBufferManager: The singleton buffer manager instance
        """
        if cls._buffer_manager is None:
            with cls._lock:
                if cls._buffer_manager is None:
                    memory_limit = get_memory_limit()
                    # Use 80% of container memory for buffer allocations
                    buffer_memory_limit = int(memory_limit * 0.8)
                    cls._buffer_manager = BoundedBufferManager(max_memory_limit=buffer_memory_limit)
                    logger.info(
                        f"BoundedBufferManager initialized with {buffer_memory_limit} bytes "
                        f"({buffer_memory_limit / (1024 * 1024):.0f} MB) limit"
                    )
        return cls._buffer_manager


class WriteMode(Enum):
    """
    Enumeration of write modes for DACSBlobfileAdapter operations.

    WRITE_DISABLED: Disable write operations, read-only mode
    WRITE_CACHE_ONLY: Write to cache only, no blob storage
    WRITE_BLOB_ONLY: Write to blob storage only, bypass cache
    WRITE_CACHE_BLOB_ASYNC: Write to cache first, async update blob storage
    WRITE_SYNC: Write to cache and blob storage synchronously (DEFAULT)
    """

    WRITE_DISABLED = "write_disabled"
    WRITE_CACHE_ONLY = "write_cache_only"
    WRITE_BLOB_ONLY = "write_blob_only"
    WRITE_CACHE_BLOB_ASYNC = "write_cache_blob_async"
    WRITE_SYNC = "write_sync"


class DACSBlobfileAdapter:
    """
    Blobfile-compatible adapter that routes operations through DACS Client.

    This wrapper provides a limited API subset compatible with blobfile, supporting only:
    - open() for reading/writing files
    - read() for reading data
    - write() for writing data
    - seek()/tell() for position management (read-mode only)
    - exists() for checking file existence
    - listdir()/scandir() for directory listing
    - stat() for file metadata

    For operations not supported by DACS Client, an OperationNotSupportedError
    is raised, similar to how blobfile handles unsupported operations.

    Thread Safety:
        Each thread should use its own adapter instance, same as
        vanilla blobfile. Concurrent readers on the same blob are safe (pread with
        per-instance offset).
    """

    def __init__(
        self,
        path: str,
        mode: str = "r",
        streaming: Optional[bool] = None,
        buffer_size: Optional[int] = None,
        cache_dir: Optional[str] = None,
        file_size: Optional[int] = None,
        write_mode: Optional[WriteMode] = None,
    ):
        """
        Open a local or remote file for reading or writing.

        Args:
            path: local or remote path. Supports multiple Azure path formats for API compatibility:
                  - az://account/container/blob (native blobfile/boostedblob format)
                  - az://account.blob.core.windows.net/container/blob (long form)
                  - https://account.blob.core.windows.net/container/blob (https format)
            mode: one of "r", "rb", "w", "wb", "a", "ab"
            streaming: True for streaming I/O, False for download/upload on open/close
            buffer_size: number of bytes to buffer
            cache_dir: directory for caching (only with streaming=False and read modes)
            file_size: size of file (optimization for remote files)
            write_mode: WriteMode enum value for write operations.
                       Defaults to WRITE_SYNC if not provided.

        """
        # Normalize Azure paths to internal format for API parity with native blobfile/boostedblob
        if _is_azure_path(path):
            self._path = normalize_azure_path(path)
        else:
            self._path = path
        self._mode = mode
        self._streaming = streaming
        self._buffer_size = buffer_size
        self._cache_dir = cache_dir
        self._file_size = file_size
        self._closed = False

        # Set write mode with WRITE_SYNC as default
        self._write_mode = write_mode if write_mode else WriteMode.WRITE_SYNC

        # Get and cache the DACSCacheClient instance with write_mode
        self._client = _ClientManager.get_instance(write_mode=self._write_mode)

        # Get the BoundedBufferManager for memory-limited buffer allocations
        self._buffer_manager = _ClientManager.get_buffer_manager()

        # Cache for file handle (only for remote paths)
        self._file_handle = None

        # Track write buffers that must be kept alive until close()
        # Tuples of (buffer, is_user_provided) - user buffers are NOT released by us
        self._write_buffers: List[Tuple[Union[ctypes.c_void_p, ctypes.Array[ctypes.c_char]], bool]] = []

        # Position tracking for seek/tell support.
        # Each adapter instance maintains its own position independent of the shared native fd.
        self._position = 0

    def __enter__(self):
        """Context manager entry."""
        # For remote paths, get and cache the file handle
        if _is_azure_path(self._path):
            # DACS Client has limited write support:
            # - Does NOT support append mode ('a', 'ab') - would require read-modify-write
            # - Does NOT support overwrite mode for existing files - O_EXCL is used in write_blob
            # - Only supports creating new files in write mode ('w', 'wb')
            # Fail early if unsupported modes are used
            if "a" in self._mode:
                raise OperationNotSupportedError(
                    "Append mode ('a', 'ab') is not supported by DACS Client. "
                    "Only read mode ('r', 'rb') and write mode ('w', 'wb') for new files are supported."
                )

            if "w" in self._mode and self._write_mode == WriteMode.WRITE_DISABLED:
                raise OperationNotSupportedError("Write operations are disabled. WriteMode is set to WRITE_DISABLED.")

            # Determine flags based on mode
            # Read mode: O_RDONLY for reading existing files
            # Write mode: O_WRONLY | O_CREAT | O_EXCL for creating new files only (no overwrite)
            # Note: Storage intercept only supports O_EXCL (not O_TRUNC) for write operations
            flags = os.O_RDONLY
            if "w" in self._mode:
                flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL

            try:
                self._file_handle = asyncio.run(self._client.get_file_handle(self._path, flags))
            except Exception as e:
                raise handle_native_error(e, operation="open", path=self._path) from e

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()

    def read(self, size: int = -1, buffer: Optional[ctypes.c_void_p] = None) -> Union[str, bytes, memoryview]:
        """
        Read data from file. Supported API.

        Args:
            size: Number of bytes to read. -1 or None reads entire file.
            buffer: Optional user-provided buffer allocated via alloc_buffer().
                   If provided, the caller is responsible for releasing it.
                   If not provided, an internal buffer is allocated and released automatically.

        Returns:
            - If buffer is provided: memoryview (zero-copy view into user buffer)
            - If no buffer provided: bytes (text mode returns str after decode)

        Memory Management:
            When using a user-provided buffer (zero-copy path):
            - The returned memoryview points directly into the user's buffer
            - The memoryview does NOT own the memory and cannot free it
            - The user MUST keep the buffer alive while using the memoryview
            - The user MUST call release_buffer(buffer) when done to free memory
            - Accessing the memoryview after release_buffer() causes undefined behavior

            When NOT using a user-provided buffer (copy path):
            - An internal buffer is allocated, data is copied to Python bytes
            - The internal buffer is automatically released before returning
            - The returned bytes object is managed by Python's garbage collector

        Example (zero-copy with user buffer):
            buffer = alloc_buffer(1024)
            try:
                with DACSBlobfileAdapter(path, "rb") as f:
                    mv = f.read(size=1024, buffer=buffer)
                    # Use memoryview here - buffer must stay alive
                    data = bytes(mv)  # Copy if needed after release
            finally:
                release_buffer(buffer)  # User responsible for freeing
                # mv is now invalid - do not access!
        """
        if not _is_azure_path(self._path):
            # Local file read
            with open(self._path, self._mode) as f:
                return f.read(size)

        # Remote blob read using cached file handle
        if self._file_handle is None:
            raise ValueError("File handle is not open. Use context manager (with statement) to open the file.")

        # Determine read size - wrap in try-except for native errors
        # When size=-1 (read all), compute remaining bytes from current position to EOF
        # rather than reading the entire file, to avoid over-allocating buffers.
        if size == -1 or size is None:
            try:
                file_size = asyncio.run(self._client.get_file_size(self._path))
            except Exception as e:
                raise handle_native_error(e, operation="get_file_size", path=self._path) from e
            read_size = max(0, file_size - self._position)
        else:
            read_size = size

        # Handle empty read case
        if read_size == 0:
            if "b" not in self._mode:
                return ""  # Text mode returns empty string
            return b""  # Binary mode returns empty bytes

        # Use user-provided buffer or allocate internal buffer via BoundedBufferManager
        user_buffer_provided = buffer is not None
        if not user_buffer_provided:
            buffer = asyncio.run(self._buffer_manager.acquire_buffer(read_size))

        try:
            # Use the cached file handle's read method directly
            # Wrap in try-except to catch native library errors
            try:
                bytes_read = asyncio.run(self._file_handle.read(buffer, read_size, self._position))
            except Exception as e:
                raise handle_native_error(e, operation="read", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_read, operation="read", path=self._path)

            # Advance position by the number of bytes actually read
            self._position += bytes_read

            if user_buffer_provided:
                # Zero-copy: return memoryview into user's buffer
                # User is responsible for buffer lifetime
                # Create a ctypes array view and wrap in memoryview
                array_type = ctypes.c_char * bytes_read
                char_array = array_type.from_buffer(buffer)
                return memoryview(char_array)
            else:
                # Copy data to Python bytes - maintains API parity with native blobfile
                # Note: ctypes._string_at.argtypes is patched at module load to use
                # c_ssize_t (64-bit) instead of c_int (32-bit), enabling reads > 2GB
                # For faster reads, use the zero-copy path with a pre-allocated buffer
                data = ctypes.string_at(buffer, bytes_read)
        finally:
            # Only release buffer if we allocated it internally
            if not user_buffer_provided:
                asyncio.run(self._buffer_manager.release_buffer(buffer))

        # Decode to string if opened in text mode (only for non-user-buffer case)
        if "b" not in self._mode:
            data = data.decode("utf-8")

        return data

    def write(
        self, data: Union[str, bytes, bytearray, memoryview, None] = None,
        size: Optional[int] = None, buffer: Optional[ctypes.c_void_p] = None
    ) -> int:
        """
        Write data to file. Supported API.

        Args:
            data: Data to write (str, bytes, bytearray, or memoryview).
                  Required if buffer is not provided.  Writable buffers
                  (bytearray, writable memoryview) use a zero-copy path;
                  readonly data (bytes, str, readonly memoryview) is copied
                  into an internally managed buffer.

                  Warning: zero-copy buffers must not be mutated by the
                  caller until close() is called, as the native layer may
                  hold a reference to the underlying memory.
            size: Number of bytes to write from buffer. Required if buffer is provided.
            buffer: Optional user-provided ctypes buffer containing data to write.
                   If provided, data parameter is ignored and size must be specified.

        Returns:
            Number of bytes written, or -1 if write failed.

        Raises:
            OperationNotSupportedError: If write_mode is WRITE_DISABLED or buffer used for local files.
            ValueError: If neither data nor buffer provided, or size missing with buffer.

        Memory Management:
            When using a user-provided buffer (zero-copy path):
            - The buffer is passed directly to the native write operation
            - The user MUST keep the buffer alive until close() is called
            - The user MUST call release_buffer(buffer) AFTER close() to free memory
            - Releasing the buffer before close() causes undefined behavior
            - DACSBlobfileAdapter tracks the buffer but does NOT release it - user is responsible

            When NOT using a user-provided buffer (copy path):
            - An internal buffer is allocated and data is copied into it
            - The internal buffer is tracked and automatically released on close()
            - No action required from user - memory is managed internally

        Why buffers must stay alive until close():
            The native storage intercept library may hold references to write buffers
            until the file is closed and data is flushed to storage. Releasing buffers
            early can cause data corruption or crashes.

        Example (zero-copy with user buffer):
            buffer = alloc_buffer(1024)
            ctypes.memmove(buffer, data, len(data))
            try:
                with DACSBlobfileAdapter(path, "wb") as f:
                    f.write(buffer=buffer, size=len(data))
                    # Buffer must stay alive until context manager exits (close)
            finally:
                release_buffer(buffer)  # Safe to release AFTER close()

        Example (automatic memory management):
            with DACSBlobfileAdapter(path, "wb") as f:
                f.write(data="Hello, World!")  # Internal buffer managed automatically
            # Internal buffer released automatically on close()
        """
        if self._write_mode == WriteMode.WRITE_DISABLED:
            raise OperationNotSupportedError("Write operations are disabled. WriteMode is set to WRITE_DISABLED.")

        if not _is_azure_path(self._path):
            # Local file write (buffer mode not supported for local files)
            if buffer is not None:
                raise OperationNotSupportedError("User-provided buffer is not supported for local file writes.")
            with open(self._path, self._mode) as f:
                return f.write(data)

        # Remote blob write using cached file handle
        if self._file_handle is None:
            raise ValueError("File handle is not open. Use context manager (with statement) to open the file.")

        # Determine if using user buffer or data
        user_buffer_provided = buffer is not None

        if user_buffer_provided:
            if size is None:
                raise ValueError("size must be specified when using user-provided buffer.")
            # Track user buffer - we don't release it, but need to document lifetime requirement
            self._write_buffers.append((buffer, True))  # True = user-provided, don't release
            try:
                bytes_written = asyncio.run(self._file_handle.write(buffer, size, self._position))
            except Exception as e:
                raise handle_native_error(e, operation="write", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_written, operation="write", path=self._path)
            self._position += bytes_written
            return bytes_written
        else:
            if data is None:
                raise ValueError("Either data or buffer must be provided.")

            # Build a ctypes-compatible buffer for the native pwrite call.
            def _copy_to_managed_buffer(src, length):
                """Acquire a BoundedBufferManager buffer, copy src into it, and track it."""
                buf = asyncio.run(self._buffer_manager.acquire_buffer(length))
                ctypes.memmove(buf, src, length)
                self._write_buffers.append((buf, False))
                return buf, length

            if isinstance(data, str):
                data_bytes = data.encode("utf-8")
                internal_buffer, data_len = _copy_to_managed_buffer(data_bytes, len(data_bytes))
            elif isinstance(data, (memoryview, bytearray)) and not getattr(data, "readonly", False):
                # Writable buffer — zero-copy via from_buffer.
                data_len = data.nbytes if isinstance(data, memoryview) else len(data)
                if isinstance(data, memoryview) and not data.contiguous:
                    # Non-contiguous memoryview cannot use from_buffer; copy instead.
                    data_bytes = bytes(data)
                    internal_buffer, data_len = _copy_to_managed_buffer(data_bytes, len(data_bytes))
                else:
                    internal_buffer = (ctypes.c_char * data_len).from_buffer(data)
                    self._write_buffers.append((internal_buffer, True))
            elif isinstance(data, bytes):
                internal_buffer, data_len = _copy_to_managed_buffer(data, len(data))
            else:
                # Readonly memoryview or other buffer-protocol objects:
                # ctypes.memmove only accepts bytes, so convert first.
                data_bytes = bytes(data)
                internal_buffer, data_len = _copy_to_managed_buffer(data_bytes, len(data_bytes))

            try:
                bytes_written = asyncio.run(self._file_handle.write(internal_buffer, data_len, self._position))
            except DACSError:
                raise
            except Exception as e:
                raise handle_native_error(e, operation="write", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_written, operation="write", path=self._path)
            self._position += bytes_written
            return bytes_written

    def readline(self) -> Union[str, bytes]:
        """Read a single line. Not supported by DACS Client."""
        raise OperationNotSupportedError("readline() is not supported by DACS Client. " "Use read() instead.")

    def readlines(self) -> List[Union[str, bytes]]:
        """Read all lines. Not supported by DACS Client."""
        raise OperationNotSupportedError("readlines() is not supported by DACS Client. " "Use read() instead.")

    def writelines(self, lines: List[Union[str, bytes]]) -> None:
        """Write multiple lines. Not supported by DACS Client."""
        raise OperationNotSupportedError(
            "writelines() is not supported by DACS Client. " "Use write() instead."
        )

    def seek(self, offset: int, whence: int = 0) -> int:
        """Seek to a position in the file (read-mode only).

        Args:
            offset: Position offset (interpretation depends on whence).
            whence: 0=SEEK_SET (absolute), 1=SEEK_CUR (relative), 2=SEEK_END (from end).

        Returns:
            The new absolute position.
        """
        if not _is_azure_path(self._path):
            raise OperationNotSupportedError(
                "seek() is not supported for local paths."
            )

        if "w" in self._mode:
            raise OperationNotSupportedError(
                "seek() is not supported in write mode. "
                "The underlying storage does not support overwrite writes."
            )

        if whence == os.SEEK_SET:
            new_position = offset
        elif whence == os.SEEK_CUR:
            new_position = self._position + offset
        elif whence == os.SEEK_END:
            try:
                file_size = asyncio.run(self._client.get_file_size(self._path))
            except Exception as e:
                raise handle_native_error(e, operation="get_file_size", path=self._path) from e
            new_position = file_size + offset
        else:
            raise ValueError(f"Invalid whence value: {whence}. Must be 0 (SEEK_SET), 1 (SEEK_CUR), or 2 (SEEK_END).")

        if new_position < 0:
            raise OSError(errno.EINVAL, "Seek would result in negative file position")

        self._position = new_position
        return self._position

    def tell(self) -> int:
        """Return the current file position."""
        if not _is_azure_path(self._path):
            raise OperationNotSupportedError(
                "tell() is not supported for local paths."
            )
        return self._position

    def seekable(self) -> bool:
        """Return True if the file supports seeking (read-mode remote paths only)."""
        return _is_azure_path(self._path) and "w" not in self._mode

    def flush(self) -> None:
        """Flush write buffers. Not supported by DACS Client."""
        raise OperationNotSupportedError("flush() is not supported by DACS Client.")

    def close(self) -> None:
        """Close the file and release all internally-allocated write buffers."""
        if not self._closed:
            # Close the file handle to trigger close in storage intercept lib which actually writes the file to cache and/or blob.
            if self._file_handle is not None:
                try:
                    asyncio.run(self._client.release_file_handle(self._file_handle))
                except Exception as e:
                    logger.error(f"Error closing file handle for '{self._path}': {e}")
                    raise handle_native_error(e, operation="close", path=self._path) from e
                finally:
                    self._file_handle = None

            # Release all internally-allocated write buffers via BoundedBufferManager
            # User-provided buffers are NOT released - caller is responsible
            for buffer, is_user_provided in self._write_buffers:
                if not is_user_provided:
                    asyncio.run(self._buffer_manager.release_buffer(buffer))
            self._write_buffers.clear()
            self._closed = True

    def __iter__(self):
        """Iterate over lines. Not supported by DACS Client."""
        raise OperationNotSupportedError("Iteration is not supported by DACS Client.")

    def __next__(self):
        """Get next line. Not supported by DACS Client."""
        raise OperationNotSupportedError("Iteration is not supported by DACS Client.")


# ============================================================================
# Blobfile API Definitions
# Reference: https://github.com/christopher-hesse/blobfile
# ============================================================================
# The following namedtuples define the return types for module-level functions.
# They match the official blobfile API exactly to ensure compatibility.
#
# class Stat(NamedTuple):
#     """File statistics returned by stat() function"""
#     size: int                   # File size in bytes
#     mtime: float                # Time of last modification
#     ctime: float                # Time of creation/metadata change
#     md5: Optional[str]          # MD5 hash (None for Azure)
#     version: Optional[str]      # Version string (None for Azure)
#
# class DirEntry(NamedTuple):
#     """Directory entry returned by scandir() function"""
#     path: str                   # Full path to the entry
#     name: str                   # Filename component
#     is_dir: bool                # True if entry is a directory
#     is_file: bool               # True if entry is a file
#     stat: Optional[Stat]        # Stat object or None if not available
#
# Extended Stat fields (beyond blobfile API):
# When using DACS Client, the stat() function also includes these fields:
#     st_atime: float             # Time of last access
#     st_mode: int                # File mode/permissions
#     st_ino: int                 # Inode number
#     st_dev: int                 # Device ID
#     st_rdev: int                # Device ID (if special file)
#     st_nlink: int               # Number of hard links
#     st_uid: int                 # User ID of owner
#     st_gid: int                 # Group ID of owner
# ============================================================================


# Module-level functions matching blobfile API

# Default chunk size for copy operations (256MB)
# Note: storageIntercept lib handles internal chunking at 32MB, so we use larger
# chunks here to reduce Python-level overhead while keeping memory usage bounded.
COPY_CHUNK_SIZE = 256 * 1024 * 1024

def copy(src: str, dst: str, overwrite: bool = False, return_md5: bool = False) -> Optional[str]:
    """
    Copy a file from one path to another.

    Supports the following scenarios:
    - Local → Local: Uses shutil.copyfile (supports overwrite)
    - Local → Remote (az://): Streams through cache layer (overwrite NOT supported)
    - Remote (az://) → Local: Streams from cache layer (supports overwrite)
    - Remote (az://) → Remote (az://): NOT supported

    Args:
        src: Source file path (local path or az:// URL)
        dst: Destination file path (local path or az:// URL)
        overwrite: If False (default), raises FileExistsError if dst exists.
                  If True, overwrites existing destination file (only for local→local and remote→local).
                  NOTE: overwrite=True is NOT supported when destination is remote (az://).
        return_md5: If True, computes and returns MD5 hash of copied data.

    Returns:
        MD5 hash string if return_md5=True, otherwise None.

    Raises:
        FileNotFoundError: If source file does not exist.
        FileExistsError: If destination exists and overwrite=False.
        IsADirectoryError: If source or destination is a directory.
        OperationNotSupportedError: If overwrite=True with remote destination.
        DACSError: For storage-related errors.
    """
    ## Filter requests
    
    # Validate paths are not directories (simple heuristic: trailing slash)
    if src.endswith("/") or dst.endswith("/"):
        raise IsADirectoryError("copy() does not support directories. Use single file paths.")

    src_is_remote = src.startswith("az://")
    dst_is_remote = dst.startswith("az://")

    # Prefilter remote to remote copies to prevent unnecessary stat calls
    if src_is_remote and dst_is_remote:
        raise OperationNotSupportedError(
            "copy() for remote-to-remote paths is not supported by DACS Client. "
        )

    # Source check: must exist and be a file (stat raises FileNotFoundError if not found)
    src_stat = stat(src)
    if stat_module.S_ISDIR(src_stat.st_mode):
        raise IsADirectoryError(f"Source is a directory: '{src}'")

    if src == dst:
        # Source and destination are the same - no-op (guaranteed to be local to local in this case)
        return _compute_file_md5(src) if return_md5 else None
    
    # Destination check
    if overwrite:
        # overwrite=True: dst cannot be remote, dst cannot be a directory
        if dst_is_remote:
            raise OperationNotSupportedError(
                "copy() with overwrite=True is not supported for remote destinations. "
            )
        try:
            dst_stat = stat(dst)
            if stat_module.S_ISDIR(dst_stat.st_mode):
                raise IsADirectoryError(f"Destination is a directory: '{dst}'")
        except FileNotFoundError:
            pass  # dst doesn't exist, which is fine for overwrite
    else:
        # overwrite=False: dst cannot exist
        if exists(dst):
            raise FileExistsError(f"Destination already exists: '{dst}'")

    # Route to appropriate copy implementation
    if not src_is_remote and not dst_is_remote:
        # Local → Local
        return _copy_local_to_local(src, dst, return_md5, overwrite=overwrite)
    elif not src_is_remote and dst_is_remote:
        # Local → Remote (benefits from cache: populates cache for future reads)
        return _copy_local_to_remote(src, dst, return_md5)
    elif src_is_remote and not dst_is_remote:
        # Remote → Local (benefits from cache: reads from cache if cached)
        return _copy_remote_to_local(src, dst, return_md5, overwrite=overwrite)
    else:
        # Should not reach here due to prefilter
        raise OperationNotSupportedError(
            "copy() for remote-to-remote paths is not supported by DACS Client. "
        )


def _atomic_write_to_local(dst: str, overwrite: bool, write_fn):
    """Write to a local file atomically using a temp file.
    
    Writes to a temporary file first, then atomically moves it to the
    final destination. Cleans up the temp file on any failure.
    
    Args:
        dst: Final destination path.
        overwrite: If True, overwrites existing dst. If False, fails if dst exists.
        write_fn: Callable that receives the temp file path and writes content to it.
    """
    # Ensure destination directory exists
    dst_dir = os.path.dirname(dst) or "."
    os.makedirs(dst_dir, exist_ok=True)

    # Use mkstemp to create a unique temp file in the same directory to avoid collisions
    fd, tmp_dst = tempfile.mkstemp(dir=dst_dir, prefix=".tmp_", suffix="_" + os.path.basename(dst))
    os.close(fd)
    try:
        write_fn(tmp_dst)

        # Atomic move to final destination
        if overwrite:
            os.replace(tmp_dst, dst)
        else:
            # Use os.link + os.unlink for atomic exclusive creation
            try:
                os.link(tmp_dst, dst)
            except FileExistsError:
                raise FileExistsError(f"Destination already exists: '{dst}'")
            finally:
                os.unlink(tmp_dst)
    except BaseException:
        # Clean up temp file on any failure
        if os.path.exists(tmp_dst):
            os.unlink(tmp_dst)
        raise


def _copy_local_to_local(src: str, dst: str, return_md5: bool, overwrite: bool = False) -> Optional[str]:
    """Copy local file to local destination."""
    def write_fn(tmp_dst):
        shutil.copyfile(src, tmp_dst)

    _atomic_write_to_local(dst, overwrite, write_fn)

    if return_md5:
        return _compute_file_md5(dst)
    return None


def _copy_local_to_remote(src: str, dst: str, return_md5: bool) -> Optional[str]:
    """Copy local file to remote destination through cache layer.
    
    Note: DACSBlobfileAdapter only supports single-shot writes (no streaming/append),
    so the entire file must be read into memory and written at once.
    """
    # Read entire file into memory (DACS doesn't support streaming writes)
    file_size = os.path.getsize(src)
    try:
        with open(src, "rb") as src_file:
            data = src_file.read()
    except MemoryError:
        raise MemoryError(
            f"Not enough memory to read '{src}' ({file_size} bytes). "
            f"Local-to-remote copy requires loading the entire file into memory."
        )

    md5_hash = None
    if return_md5:
        md5_hash = hashlib.md5(data).hexdigest()

    with DACSBlobfileAdapter(dst, "wb") as dst_adapter:
        dst_adapter.write(data=data)

    return md5_hash


def _copy_remote_to_local(src: str, dst: str, return_md5: bool, overwrite: bool = False) -> Optional[str]:
    """Copy remote file to local destination from cache layer."""
    md5_hash = hashlib.md5() if return_md5 else None

    # Get source file size for chunked reading
    client = _ClientManager.get_instance()
    try:
        src_size = asyncio.run(client.get_file_size(src))
    except Exception as e:
        raise handle_native_error(e, operation="get_file_size", path=src) from e

    def write_fn(tmp_dst):
        buffer_manager = _ClientManager.get_buffer_manager()
        with DACSBlobfileAdapter(src, "rb") as src_adapter:
            with open(tmp_dst, "wb") as dst_file:
                offset = 0
                while offset < src_size:
                    chunk_size = min(COPY_CHUNK_SIZE, src_size - offset)
                    # Allocate buffer for this chunk via BoundedBufferManager
                    buffer = asyncio.run(buffer_manager.acquire_buffer(chunk_size))
                    try:
                        # Read chunk - need to use the file handle directly for offset reads
                        try:
                            bytes_read = asyncio.run(src_adapter._file_handle.read(buffer, chunk_size, offset))
                        except Exception as e:
                            raise handle_native_error(e, operation="read", path=src) from e
                        check_native_result(bytes_read, operation="read", path=src)
                        if bytes_read == 0:
                            break
                        # Create zero-copy memoryview over the C buffer
                        chunk_data = memoryview(buffer)[:bytes_read]
                        if md5_hash:
                            md5_hash.update(chunk_data)
                        dst_file.write(chunk_data)
                        offset += bytes_read
                    finally:
                        asyncio.run(buffer_manager.release_buffer(buffer))

    _atomic_write_to_local(dst, overwrite, write_fn)

    return md5_hash.hexdigest() if md5_hash else None


def _compute_file_md5(path: str) -> str:
    """Compute MD5 hash of a local file."""
    md5_hash = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(COPY_CHUNK_SIZE), b""):
            md5_hash.update(chunk)
    return md5_hash.hexdigest()


def exists(path: str) -> bool:
    """Check if a file or directory exists. Supported API."""
    if not _is_azure_path(path):
        # Local file check
        return os.path.exists(path)

    # Normalize path for remote blob check
    normalized_path = normalize_azure_path(path)
    
    # Remote blob check using DACSCacheClient
    try:
        client = _ClientManager.get_instance()
        asyncio.run(client.get_file_stat(normalized_path))
        return True
    except DACSNotFoundError:
        return False
    except Exception as e:
        converted = handle_native_error(e, operation="stat", path=path)
        if isinstance(converted, DACSNotFoundError):
            return False
        raise converted from e


def glob(pattern: str) -> Iterator[str]:
    """Return files matching a glob pattern. Not supported by DACS Client."""
    raise OperationNotSupportedError(
        "glob() is not supported by DACS Client. " "Use listdir() or scandir() instead."
    )


def scanglob(pattern: str) -> Iterator[str]:
    """Return files matching a glob pattern (generator). Not supported by DACS Client."""
    raise OperationNotSupportedError(
        "scanglob() is not supported by DACS Client. " "Use listdir() or scandir() instead."
    )


def isdir(path: str) -> bool:
    """Check if path is a directory.

    Uses stat() + S_ISDIR to determine if the path is a directory.
    Returns False if the path does not exist.
    """
    if not _is_azure_path(path):
        return os.path.isdir(path)

    try:
        result = stat(path)
        return stat_module.S_ISDIR(result.st_mode)
    except DACSNotFoundError:
        return False


def listdir(path: str) -> Iterator[str]:
    """List contents of a directory. Supported API."""
    if not _is_azure_path(path):
        # Local directory listing
        for item in os.listdir(path):
            yield item
    else:
        # Normalize path for remote blob directory listing
        normalized_path = normalize_azure_path(path)
        
        # Remote blob directory listing using DACSCacheClient
        async def _listdir_async():
            result = []
            client = _ClientManager.get_instance()
            async for entry in client.list_dir(normalized_path):
                # entry is a Dict[str, object] with 'd_name' field containing the filename
                if "d_name" in entry:
                    name = entry["d_name"]
                    # Filter out '.' and '..' entries - these don't exist in blob storage
                    # They are artifacts from the storage intercept POSIX emulation layer
                    if name not in (".", ".."):
                        result.append(name)
            return result

        try:
            for item in asyncio.run(_listdir_async()):
                yield item
        except Exception as e:
            raise handle_native_error(e, operation="listdir", path=path) from e


def scandir(path: str) -> Iterator[str]:
    """
    List contents of a directory (generator). Supported API.
    Returns DirEntry objects with path, name, is_dir, is_file, and stat fields.
    
    Args:
        path: local or remote path. Supports multiple Azure path formats.
    """

    # Match blobfile's DirEntry namedtuple
    DirEntry = namedtuple(
        "DirEntry",
        [
            "path",    # Full path to the entry
            "name",    # Filename component
            "is_dir",  # True if directory
            "is_file", # True if file
            "stat",    # Stat object (or None)
        ],
    )

    if not _is_azure_path(path):
        # Local directory listing
        for entry in os.scandir(path):
            yield entry.name
    else:
        # Normalize path for remote blob directory listing
        normalized_path = normalize_azure_path(path)
        
        # Remote blob directory listing using DACSCacheClient
        async def _scandir_collect():
            result = []
            client = _ClientManager.get_instance()
            async for entry in client.list_dir(normalized_path):
                # entry is a Dict[str, object] with 'd_name' field containing the filename
                if "d_name" in entry:
                    name = entry["d_name"]
                    # Filter out '.' and '..' entries - these don't exist in blob storage
                    # They are artifacts from the storage intercept POSIX emulation layer
                    if name not in (".", ".."):
                        result.append(name)
            return result

        try:
            for item in asyncio.run(_scandir_collect()):
                # Construct full path
                if path.endswith("/"):
                    full_path = path + item
                else:
                    full_path = path + "/" + item

                # Determine if directory based on trailing slash
                is_dir = item.endswith("/")

                # Get stat information for the entry
                try:
                    entry_stat = stat(full_path)
                except Exception:
                    # If stat fails, set to None
                    entry_stat = None

                yield DirEntry(
                    path=full_path,
                    name=item,
                    is_dir=is_dir,
                    is_file=not is_dir,
                    stat=entry_stat,
                )
        except Exception as e:
            raise handle_native_error(e, operation="scandir", path=path) from e


def makedirs(path: str) -> None:
    """Create directory and all parent directories. Not supported by DACS Client."""
    raise OperationNotSupportedError("makedirs() is not supported by DACS Client.")


def remove(path: str) -> None:
    """Remove a file. Not supported by DACS Client."""
    raise OperationNotSupportedError("remove() is not supported by DACS Client.")


def rmdir(path: str) -> None:
    """Remove an empty directory. Not supported by DACS Client."""
    raise OperationNotSupportedError("rmdir() is not supported by DACS Client.")


def rmtree(path: str) -> None:
    """Remove a directory tree. Not supported by DACS Client."""
    raise OperationNotSupportedError("rmtree() is not supported by DACS Client.")


def stat(path: str) -> Any:
    """Get file statistics. Supported API."""
    if not _is_azure_path(path):
        # Local file statistics
        return os.stat(path)

    # Normalize path for remote blob statistics
    normalized_path = normalize_azure_path(path)
    
    # Remote blob statistics using DACSCacheClient
    Stat = namedtuple(
        "Stat",
        [
            # Blobfile API fields (first 5)
            "size",    # File size in bytes
            "mtime",   # Time of last modification
            "ctime",   # Time of creation/metadata change
            "md5",     # MD5 hash (None for Azure)
            "version", # Version string (None for Azure)
            # Additional stat fields from DACS client
            "st_atime",  # Time of last access
            "st_mode",   # File mode/permissions
            "st_ino",    # Inode number
            "st_dev",    # Device ID
            "st_rdev",   # Device ID (if special file)
            "st_nlink",  # Number of hard links
            "st_uid",    # User ID of owner
            "st_gid",    # Group ID of owner
        ],
    )

    client = _ClientManager.get_instance()
    try:
        stat_result = asyncio.run(client.get_file_stat(normalized_path))
    except Exception as e:
        raise handle_native_error(e, operation="stat", path=path) from e

    return Stat(
        size=stat_result.st_size,
        mtime=stat_result.st_mtime,
        ctime=stat_result.st_ctime,
        md5=None,      # Azure doesn't provide MD5 via stat
        version=None,  # Azure doesn't provide version via stat
        st_atime=stat_result.st_atime,
        st_mode=stat_result.st_mode,
        st_ino=0,      # Not provided by DACS
        st_dev=0,      # Not provided by DACS
        st_rdev=0,     # Not provided by DACS
        st_nlink=0,    # Not provided by DACS
        st_uid=0,      # Not provided by DACS
        st_gid=0,      # Not provided by DACS
    )


def walk(top: str, topdown: bool = True) -> Iterator[Tuple[str, List[str], List[str]]]:
    """Walk directory tree. Not supported by DACS Client."""
    raise OperationNotSupportedError(
        "walk() is not supported by DACS Client. " "Use listdir() or scandir() instead."
    )


def basename(path: str) -> str:
    """Get final component of path. Not supported by DACS Client."""
    raise OperationNotSupportedError("basename() is not supported by DACS Client.")


def dirname(path: str) -> str:
    """Get directory name from path. Not supported by DACS Client."""
    raise OperationNotSupportedError("dirname() is not supported by DACS Client.")


def join(*paths: str) -> str:
    """Join path components. Not supported by DACS Client."""
    raise OperationNotSupportedError("join() is not supported by DACS Client.")


def get_url(path: str, expiration: int = 3600) -> Tuple[str, Optional[float]]:
    """
    Get a URL for a path.
    Not supported by DACS Client.
    """
    raise OperationNotSupportedError("get_url() is not supported by DACS Client.")


def md5(path: str) -> str:
    """Get MD5 hash of a file. Not supported by DACS Client."""
    raise OperationNotSupportedError("md5() is not supported by DACS Client.")


def set_mtime(path: str, mtime: float) -> None:
    """Set modification time of a file. Not supported by DACS Client."""
    raise OperationNotSupportedError("set_mtime() is not supported by DACS Client.")

def configure(**kwargs) -> None:
    """
    Configure global settings.
    
    DACS Client supports a limited subset of blobfile configuration options.
    Unsupported options will raise OperationNotSupportedError.
    
    Supported options:
        - log_callback: Custom logging callback function
        
    Unsupported options (will raise OperationNotSupportedError):
        - connection_pool_max_size: Connection pooling managed by native library
        - max_connection_pool_count: Connection pooling managed by native library
        - azure_write_chunk_size: Chunk size managed by native library
        - google_write_chunk_size: Google Cloud Storage not supported
        - retry_limit: Retry logic managed by native library
        - retry_log_threshold: Retry logic managed by native library
        - connect_timeout: Timeouts managed by native library
        - read_timeout: Timeouts managed by native library
        - output_az_paths: Path format is fixed
        - use_azure_storage_account_key_fallback: Auth managed by native library
    
    Args:
        **kwargs: Configuration options
        
    Raises:
        OperationNotSupportedError: If an unsupported configuration option is provided
    """
    # Options that are not supported by DACS Client
    unsupported_options = {
        "connection_pool_max_size": "Connection pooling is managed by the native library",
        "max_connection_pool_count": "Connection pooling is managed by the native library",
        "azure_write_chunk_size": "Chunk size is managed by the native library",
        "google_write_chunk_size": "Google Cloud Storage is not supported",
        "retry_limit": "Retry logic is managed by the native library",
        "retry_common_log_threshold": "Retry logic is managed by the native library",
        "retry_log_threshold": "Retry logic is managed by the native library",
        "connect_timeout": "Timeouts are managed by the native library",
        "read_timeout": "Timeouts are managed by the native library",
        "output_az_paths": "Azure path format is fixed",
        "use_azure_storage_account_key_fallback": "Authentication is managed by the native library",
        "get_http_pool_options": "HTTP pool options are managed by the native library",
        "get_gcs_http_pool_options": "Google Cloud Storage is not supported",
    }
    
    # Check for unsupported options
    for key in kwargs:
        if key in unsupported_options:
            raise OperationNotSupportedError(
                f"configure('{key}') is not supported by DACS Client. "
                f"Reason: {unsupported_options[key]}"
            )
    
    # Handle supported options
    if "log_callback" in kwargs:
        # Set custom log callback if provided
        callback = kwargs["log_callback"]
        if callback is not None:
            # Configure logging to use the provided callback
            handler = logging.Handler()
            handler.emit = lambda record: callback(record.getMessage())
            logger.addHandler(handler)
    
    # Silently ignore unknown options for forward compatibility
    # This matches blobfile's behavior of being permissive with unknown options


def shutdown() -> None:
    """
    Shutdown the DACS Client and clean up all resources.

    This function should be called before terminating the application to ensure:
    - All open file handles are properly closed
    - Metrics are logged
    - Resources are cleaned up

    After calling this function, the client singleton will need to be reinitialized
    for any subsequent operations.
    """
    client = _ClientManager.get_instance()
    asyncio.run(client.shutdown())


# Alias for drop-in blobfile compatibility
# Users can do: from dacs_adapter import BlobFile
BlobFile = DACSBlobfileAdapter
