#!/usr/bin/env python3
# --------------------------------------------------------------------------------------------------------------------
# DACS Client - Blobfile API
# Provides blobfile-compatible API for dacs_client
# --------------------------------------------------------------------------------------------------------------------

import asyncio
import ctypes
import errno
import logging
import os
import threading
import time
from collections import namedtuple
from typing import Optional, Union, Iterator, List, Any, Tuple
from enum import Enum

from py_dacs_client.dacs_client import DACSCacheClient
from py_dacs_client.dacs_utils import alloc_buffer, release_buffer
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
    "DACSAdapter",
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


class _ClientManager:
    """Thread-safe singleton manager for DACSCacheClient instance."""

    _instance = None
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


class WriteMode(Enum):
    """
    Enumeration of write modes for DACSAdapter operations.

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


class DACSAdapter:
    """
    Blobfile-compatible adapter that routes operations through DACS Client.

    This wrapper provides a limited API subset compatible with blobfile, supporting only:
    - open() for reading/writing files
    - read() for reading data
    - write() for writing data
    - exists() for checking file existence
    - listdir()/scandir() for directory listing
    - stat() for file metadata

    For operations not supported by DACS Client, an OperationNotSupportedError
    is raised, similar to how blobfile handles unsupported operations.
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
            path: local or remote path
            mode: one of "r", "rb", "w", "wb", "a", "ab"
            streaming: True for streaming I/O, False for download/upload on open/close
            buffer_size: number of bytes to buffer
            cache_dir: directory for caching (only with streaming=False and read modes)
            file_size: size of file (optimization for remote files)
            write_mode: WriteMode enum value for write operations.
                       Defaults to WRITE_SYNC if not provided.

        """
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

        # Cache for file handle (only for remote paths)
        self._file_handle = None

        # Track write buffers that must be kept alive until close()
        # Tuples of (buffer, is_user_provided) - user buffers are NOT released by us
        self._write_buffers: List[Tuple[ctypes.c_void_p, bool]] = []

    def __enter__(self):
        """Context manager entry."""
        # For remote paths, get and cache the file handle
        if self._path.startswith("az://"):
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
            except DACSError:
                # Re-raise our own exceptions as-is
                raise
            except Exception as e:
                raise handle_native_error(e, operation="open", path=self._path) from e

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        # For remote paths, release the cached file handle
        if self._file_handle is not None:
            try:
                asyncio.run(self._client.release_file_handle(self._file_handle))
            except Exception as e:
                # Log but don't raise - we're in cleanup
                logger.warning(f"Error releasing file handle for '{self._path}': {e}")
            finally:
                self._file_handle = None

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
                with DACSAdapter(path, "rb") as f:
                    mv = f.read(size=1024, buffer=buffer)
                    # Use memoryview here - buffer must stay alive
                    data = bytes(mv)  # Copy if needed after release
            finally:
                release_buffer(buffer)  # User responsible for freeing
                # mv is now invalid - do not access!
        """
        if not self._path.startswith("az://"):
            # Local file read
            with open(self._path, self._mode) as f:
                return f.read(size)

        # Remote blob read using cached file handle
        if self._file_handle is None:
            raise ValueError("File handle is not open. Use context manager (with statement) to open the file.")

        # Determine read size - wrap in try-except for native errors
        try:
            read_size = asyncio.run(self._client.get_file_size(self._path)) if (size == -1 or size is None) else size
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="get_file_size", path=self._path) from e

        # Handle empty file case
        if read_size == 0:
            if "b" not in self._mode:
                return ""  # Text mode returns empty string
            return b""  # Binary mode returns empty bytes

        # Use user-provided buffer or allocate internal buffer
        user_buffer_provided = buffer is not None
        if not user_buffer_provided:
            buffer = alloc_buffer(read_size)

        try:
            # Use the cached file handle's read method directly
            # Wrap in try-except to catch native library errors
            try:
                bytes_read = asyncio.run(self._file_handle.read(buffer, read_size, 0))
            except DACSError:
                raise
            except Exception as e:
                raise handle_native_error(e, operation="read", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_read, operation="read", path=self._path)

            if user_buffer_provided:
                # Zero-copy: return memoryview into user's buffer
                # User is responsible for buffer lifetime
                # Create a ctypes array view and wrap in memoryview
                array_type = ctypes.c_char * bytes_read
                char_array = array_type.from_buffer(buffer)
                return memoryview(char_array)
            else:
                # Copy data to Python bytes (buffer will be released in finally)
                data = ctypes.string_at(buffer, bytes_read)
        finally:
            # Only release buffer if we allocated it internally
            if not user_buffer_provided:
                release_buffer(buffer)

        # Decode to string if opened in text mode (only for non-user-buffer case)
        if "b" not in self._mode:
            data = data.decode("utf-8")

        return data

    def write(
        self, data: Union[str, bytes, None] = None, size: Optional[int] = None, buffer: Optional[ctypes.c_void_p] = None
    ) -> int:
        """
        Write data to file. Supported API.

        Args:
            data: Data to write (str or bytes). Required if buffer is not provided.
            size: Number of bytes to write from buffer. Required if buffer is provided.
            buffer: Optional user-provided buffer containing data to write.
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
            - DACSAdapter tracks the buffer but does NOT release it - user is responsible

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
                with DACSAdapter(path, "wb") as f:
                    f.write(buffer=buffer, size=len(data))
                    # Buffer must stay alive until context manager exits (close)
            finally:
                release_buffer(buffer)  # Safe to release AFTER close()

        Example (automatic memory management):
            with DACSAdapter(path, "wb") as f:
                f.write(data="Hello, World!")  # Internal buffer managed automatically
            # Internal buffer released automatically on close()
        """
        if self._write_mode == WriteMode.WRITE_DISABLED:
            raise OperationNotSupportedError("Write operations are disabled. WriteMode is set to WRITE_DISABLED.")

        if not self._path.startswith("az://"):
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
                bytes_written = asyncio.run(self._file_handle.write(buffer, size, 0))
            except DACSError:
                raise
            except Exception as e:
                raise handle_native_error(e, operation="write", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_written, operation="write", path=self._path)
            return bytes_written
        else:
            if data is None:
                raise ValueError("Either data or buffer must be provided.")

            # Convert data to bytes if needed
            if isinstance(data, str):
                data_bytes = data.encode("utf-8")
            else:
                data_bytes = data

            # Allocate buffer and copy data - DO NOT release until close()
            internal_buffer = alloc_buffer(len(data_bytes))
            ctypes.memmove(internal_buffer, data_bytes, len(data_bytes))

            # Track internal buffer for release on close()
            self._write_buffers.append((internal_buffer, False))  # False = internal, release on close

            try:
                bytes_written = asyncio.run(self._file_handle.write(internal_buffer, len(data_bytes), 0))
            except DACSError:
                raise
            except Exception as e:
                raise handle_native_error(e, operation="write", path=self._path) from e

            # Check for error return value
            check_native_result(bytes_written, operation="write", path=self._path)
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
        """Seek to position in file. Not supported by DACS Client."""
        raise OperationNotSupportedError("seek() is not supported by DACS Client.")

    def tell(self) -> int:
        """Get current file position. Not supported by DACS Client."""
        raise OperationNotSupportedError("tell() is not supported by DACS Client.")

    def flush(self) -> None:
        """Flush write buffers. Not supported by DACS Client."""
        raise OperationNotSupportedError("flush() is not supported by DACS Client.")

    def close(self) -> None:
        """Close the file and release all internally-allocated write buffers."""
        if not self._closed:
            # Release all internally-allocated write buffers
            # User-provided buffers are NOT released - caller is responsible
            for buffer, is_user_provided in self._write_buffers:
                if not is_user_provided:
                    release_buffer(buffer)
            self._write_buffers.clear()
            self._closed = True

    def __iter__(self):
        """Iterate over lines. Not supported by DACS Client."""
        raise OperationNotSupportedError("Iteration is not supported by DACS Client.")

    def __next__(self):
        """Get next line. Not supported by DACS Client."""
        raise OperationNotSupportedError("Iteration is not supported by DACS Client.")


# Module-level functions matching blobfile API
def copy(src: str, dst: str, overwrite: bool = False, return_md5: bool = False) -> Optional[str]:
    """
    Copy a file from one path to another.
    Not supported by DACS Client.
    """
    raise OperationNotSupportedError("copy() is not supported by DACS Client.")


def exists(path: str) -> bool:
    """Check if a file or directory exists. Supported API."""
    if not path.startswith("az://"):
        # Local file check
        return os.path.exists(path)

    # Remote blob check using DACSCacheClient
    try:
        client = _ClientManager.get_instance()
        asyncio.run(client.get_file_stat(path))
        return True
    except OSError as e:
        # Check if the error is due to file not found (ENOENT)
        if e.errno == errno.ENOENT:
            return False
        # For other errors, re-raise
        raise


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
    """Check if path is a directory. Not supported by DACS Client."""
    raise OperationNotSupportedError("isdir() is not supported by DACS Client. " "Use stat() instead.")


def listdir(path: str) -> Iterator[str]:
    """List contents of a directory. Supported API."""
    if not path.startswith("az://"):
        # Local directory listing
        for item in os.listdir(path):
            yield item
    else:
        # Remote blob directory listing using DACSCacheClient
        async def _listdir_async():
            result = []
            client = _ClientManager.get_instance()
            async for entry in client.list_dir(path):
                # entry is a Dict[str, object] with 'd_name' field containing the filename
                if "d_name" in entry:
                    result.append(entry["d_name"])
            return result

        for item in asyncio.run(_listdir_async()):
            yield item


def scandir(path: str) -> Iterator[str]:
    """List contents of a directory (generator). Supported API."""
    if not path.startswith("az://"):
        # Local directory listing
        for entry in os.scandir(path):
            yield entry.name
    else:
        # Remote blob directory listing using DACSCacheClient
        async def _scandir_collect():
            result = []
            client = _ClientManager.get_instance()
            async for entry in client.list_dir(path):
                # entry is a Dict[str, object] with 'd_name' field containing the filename
                if "d_name" in entry:
                    result.append(entry["d_name"])
            return result

        for item in asyncio.run(_scandir_collect()):
            yield item


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
    if not path.startswith("az://"):
        # Local file statistics
        return os.stat(path)

    # Remote blob statistics using DACSCacheClient
    # Create a stat-like object with common fields from os.stat_result
    BlobStat = namedtuple(
        "BlobStat",
        [
            "st_size",  # File size in bytes
            "st_mtime",  # Time of last modification
            "st_atime",  # Time of last access
            "st_ctime",  # Time of last metadata change (creation time on Windows)
            "st_mode",  # File mode/permissions
            "st_ino",  # Inode number
            "st_dev",  # Device ID
            "st_rdev",  # Device ID (if special file)
            "st_nlink",  # Number of hard links
            "st_uid",  # User ID of owner
            "st_gid",  # Group ID of owner
        ],
    )

    client = _ClientManager.get_instance()
    # Use the new get_file_stat API to get complete statistics
    stat_result = asyncio.run(client.get_file_stat(path))

    # Convert Stat64 to BlobStat namedtuple
    return BlobStat(
        st_size=stat_result.st_size,
        st_mtime=stat_result.st_mtime,
        st_atime=stat_result.st_atime,
        st_ctime=stat_result.st_ctime,
        st_mode=stat_result.st_mode,
        st_ino=stat_result.st_ino,
        st_dev=stat_result.st_dev,
        st_rdev=stat_result.st_rdev,
        st_nlink=stat_result.st_nlink,
        st_uid=stat_result.st_uid,
        st_gid=stat_result.st_gid,
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
