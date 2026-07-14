#!/usr/bin/env python3
# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# DACS Client - Boostedblob Adapter
# Provides boostedblob-compatible async API for DACSCacheClient
# --------------------------------------------------------------------------------------------------------------------
# This module provides API parity with native boostedblob, backed by DACSCacheClient.
# It supports the same function signatures and module structure as boostedblob, but routes
# all operations through DACSCacheClient instead of directly to Azure.
#
# Usage:
#   import dacs_boostedblob_adapter as bbb
#
#   # Submodule access (same as native boostedblob)
#   data = await bbb.read.read_single("az://account/container/blob.txt")
#   await bbb.write.write_single("az://account/container/new_blob.txt", b"data")
#
#   await bbb.shutdown()
#
# Fully Supported Operations:
#   - read_single(path) - Read entire blob content
#   - read_byte_range(path, byte_range) - Read byte range from blob
#   - read_stream(path, executor, size) - Streaming read (yields chunks)
#   - read_stream_unordered(path, executor, size) - Unordered streaming read
#   - read_chunked(path, executor, size) - Read using chunked streaming
#   - write_single(path, data, overwrite) - Write data to blob (new files only)
#   - exists(path) - Check if blob exists
#   - stat(path) - Get blob metadata
#   - getsize(path) - Get blob size
#   - isdir(path) - Check if path is a directory
#   - isfile(path) - Check if path is a file
#   - listdir(path) - List directory contents (async iterator)
#   - scandir(path) - Scan directory (yields DirEntry objects)
#   - listtree(path) - List all files recursively
#   - scantree(path) - Scan all files recursively
#   - shutdown() - Clean up resources
#
# Unsupported Operations (raise OperationNotSupportedError):
#   - write_stream() - No append/streaming write support in DACSCacheClient
#   - write_stream_unordered() - No append/streaming write support
#   - write_chunked() - No append/streaming write support
#   - remove() - Delete operations not supported
#   - rmtree() - Delete operations not supported
#   - copyfile() - Copy operations not supported
#   - copytree() - Copy operations not supported
#   - sync() - Sync operations not supported
#   - get_url() - SAS token generation not in scope
#   - list_blobs() - Use scandir() instead
#   - glob_scandir() - Glob patterns not supported
#
# Submodules (for boostedblob API parity):
#   - read - Contains read_single, read_byte_range, read_stream, etc.
#   - write - Contains write_single, write_stream (stub), etc.
#   - share - Contains get_url (stub)
#
# Path Support:
#   - AzurePath - Fully supported
#   - LocalPath, GooglePath - Not supported (DACSCacheClient is Azure-only)
# --------------------------------------------------------------------------------------------------------------------

import asyncio
import ctypes
import datetime
import errno
import itertools
import logging
import os
import re
import stat as stat_module
import sys
import threading
import types
from dataclasses import dataclass
from typing import (
    Any,
    AsyncIterator,
    Callable,
    Iterator,
    List,
    Optional,
    Protocol,
    Tuple,
    TypeVar,
    Union,
    runtime_checkable,
)

from py_dacs_client.dacs_client import DACSCacheClient, normalize_azure_path
from py_dacs_client.dacs_utils import WriteMode, alloc_buffer, release_buffer, safe_string_at
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
)

# Type aliases matching boostedblob
ByteRange = Tuple[int, int]
OptByteRange = Tuple[Optional[int], Optional[int]]

# Constants matching boostedblob
MB = 2**20
DEFAULT_CHUNK_SIZE = 32 * MB  # 32MB default chunk size

# Type variable for generic functions
T = TypeVar("T")
F = TypeVar("F", bound=Callable[..., Any])

# Re-export following native boostedblob's __init__.py structure
# Read/write functions are accessed via submodules: bbb.read.read_single(), bbb.write.write_single()
__all__ = [
    # Submodules (synthetic) - contains read/write functions
    "read",
    "write", 
    "share",
    # BoostExecutor and session management
    "BoostExecutor",
    "ensure_session",
    # Copy/Delete/Sync operations (stubs - raise OperationNotSupportedError)
    "copyfile",
    "copytree",
    "remove",
    "rmtree",
    "sync",
    # Listing functions
    "listdir",
    "scandir",
    "listtree",
    "scantree",
    # Path classes
    "AzurePath",
    "BasePath",
    "BlobPath",
    "CloudPath",
    "GooglePath",
    "LocalPath",
    # Path functions
    "exists",
    "getsize",
    "isdir",
    "isfile",
    "stat",
    # Utility
    "shutdown",
    # Types
    "Stat",
    "DirEntry",
    "WriteMode",
    "ByteRange",
    "OptByteRange",
    # Exceptions (for user convenience)
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
    "OperationNotSupportedError",
]

logger = logging.getLogger(__name__)

# ==============================
# Path Classes (matching boostedblob)
# ==============================


@dataclass(frozen=True)
class BasePath:
    """Base class for paths, matching boostedblob's BasePath interface."""

    @staticmethod
    def from_str(path: str) -> "BasePath":
        """Create a path from a string."""
        if path.startswith("az://"):
            return AzurePath.from_str(path)
        raise ValueError(f"Unsupported path scheme: {path}")

    @property
    def name(self) -> str:
        """Returns the name of path (last component)."""
        raise NotImplementedError

    def __str__(self) -> str:
        raise NotImplementedError


@dataclass(frozen=True)
class AzurePath(BasePath):
    """Azure blob path matching boostedblob's AzurePath interface."""

    account: str
    container: str
    blob: str

    @staticmethod
    def from_str(url: str) -> "AzurePath":
        """Parse an Azure path string into AzurePath.
        
        Supports both URL formats matching native boostedblob:
        - az://account/container/blob (short form)
        - az://account.blob.core.windows.net/container/blob (long form)
        - https://account.blob.core.windows.net/container/blob (https form)
        """
        import urllib.parse
        
        parsed_url = urllib.parse.urlparse(url)
        if parsed_url.scheme == "https":
            account, host = parsed_url.netloc.split(".", maxsplit=1)
            if host != "blob.core.windows.net":
                raise ValueError(f"Invalid URL '{url}'; unexpected host '{host}'")
        elif parsed_url.scheme == "az":
            # Handle both az://account and az://account.blob.core.windows.net
            netloc = parsed_url.netloc
            suffix = ".blob.core.windows.net"
            if netloc.endswith(suffix):
                account = netloc[:-len(suffix)]
            else:
                account = netloc
        else:
            raise ValueError(f"Invalid URL '{url}'; expected 'https' or 'az' scheme")

        if not account:
            raise ValueError(f"Invalid URL '{url}'; expected account name")

        # Split the unparsed URL to get container and blob
        parts = url.split("/", maxsplit=4)
        container = parts[3] if len(parts) >= 4 else ""
        blob = parts[4] if len(parts) >= 5 else ""
        return AzurePath(account=account, container=container, blob=blob)

    @property
    def name(self) -> str:
        """Returns the name of path (last component)."""
        if self.blob:
            return os.path.basename(self.blob.rstrip("/"))
        if self.container:
            return self.container
        return self.account

    @property
    def parent(self) -> "AzurePath":
        """Returns the parent path."""
        if self.blob:
            parent_blob = os.path.dirname(self.blob.rstrip("/"))
            return AzurePath(self.account, self.container, parent_blob)
        return AzurePath(self.account, "", "")

    def is_directory_like(self) -> bool:
        """Check if path looks like a directory."""
        return not self.blob or self.blob.endswith("/")

    def ensure_directory_like(self) -> "AzurePath":
        """Ensure path ends with /."""
        if self.is_directory_like():
            return self
        return AzurePath(self.account, self.container, self.blob + "/")

    def relative_to(self, other: "AzurePath") -> str:
        """Get relative path from other."""
        if not isinstance(other, AzurePath):
            raise ValueError(f"'{other}' is not an AzurePath")
        other = other.ensure_directory_like()
        if self.account != other.account or self.container != other.container:
            raise ValueError(f"'{other}' is not a subpath of '{self}'")
        if not self.blob.startswith(other.blob):
            raise ValueError(f"'{other}' is not a subpath of '{self}'")
        return self.blob[len(other.blob) :]

    def __truediv__(self, relative_path: str) -> "AzurePath":
        """Join path with /."""
        new_blob = os.path.join(self.blob, relative_path) if self.blob else relative_path
        return AzurePath(self.account, self.container, new_blob)

    def __str__(self) -> str:
        """Return short-form path string."""
        if self.blob:
            return f"az://{self.account}/{self.container}/{self.blob}"
        if self.container:
            return f"az://{self.account}/{self.container}"
        return f"az://{self.account}"

    def to_long_form(self) -> str:
        """Return long-form path for DACSCacheClient."""
        if self.blob:
            return f"az://{self.account}.blob.core.windows.net/{self.container}/{self.blob}"
        if self.container:
            return f"az://{self.account}.blob.core.windows.net/{self.container}"
        return f"az://{self.account}.blob.core.windows.net"

    def format_url(self, template: str) -> str:
        """Format a URL template with path components (for API compatibility)."""
        import urllib.parse
        escaped_data = {
            "account": urllib.parse.quote(self.account, safe=""),
            "container": urllib.parse.quote(self.container, safe=""),
            "blob": urllib.parse.quote(self.blob, safe=""),
        }
        return template.format(**escaped_data)


# Type aliases for API compatibility with native boostedblob.
# DACSCacheClient only supports Azure paths - these aliases allow code that 
# uses boostedblob type hints to work, but operations on non-Azure paths will fail.
CloudPath = AzurePath
BlobPath = Union[str, BasePath]  # Any string or BasePath can be used as a blob path
LocalPath = None  # Not supported
GooglePath = None  # Not supported


@dataclass
class Stat:
    """
    Blob metadata matching boostedblob's Stat interface.

    Attributes:
        size: File size in bytes
        mtime: Modification time (Unix timestamp)
        ctime: Creation time (Unix timestamp)
        md5: MD5 hash (not available via DACSCacheClient, always None)
        version: Blob version (not available via DACSCacheClient, always None)
    """

    size: int
    mtime: float
    ctime: float
    md5: Optional[str] = None
    version: Optional[str] = None


@dataclass
class DirEntry:
    """
    Directory entry matching boostedblob's DirEntry interface.

    Attributes:
        path: Full path to the entry (BasePath type)
        is_dir: Whether the entry is a directory
        is_file: Whether the entry is a file
        stat: Stat information if available
    """

    path: BasePath
    is_dir: bool
    is_file: bool
    stat: Optional[Stat] = None

    @staticmethod
    def from_dirpath(path: BasePath) -> "DirEntry":
        """Create DirEntry for a directory."""
        return DirEntry(path=path, is_dir=True, is_file=False, stat=None)

    @staticmethod
    def from_path_stat(path: BasePath, stat: Stat) -> "DirEntry":
        """Create DirEntry for a file with stat."""
        return DirEntry(path=path, is_dir=False, is_file=True, stat=stat)


class _ClientManager:
    """Thread-safe singleton manager for DACSCacheClient instance."""

    _instance: Optional[DACSCacheClient] = None
    _lock = threading.Lock()
    _write_mode: Optional[WriteMode] = None
    _consistent_mode: bool = False

    @classmethod
    def get_instance(cls, write_mode: Optional[WriteMode] = None, consistent_mode: bool = False) -> DACSCacheClient:
        """
        Get or create the singleton DACSCacheClient instance.
        Thread-safe lazy initialization using double-checked locking pattern.

        Args:
            write_mode: Write mode configuration for the client
            consistent_mode: Enable consistent mode

        Returns:
            DACSCacheClient: The singleton instance
        """
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._write_mode = write_mode if write_mode else WriteMode.WRITE_SYNC
                    cls._consistent_mode = consistent_mode
                    cls._instance = DACSCacheClient(write_mode=cls._write_mode, consistent_mode=cls._consistent_mode)
        return cls._instance

    @classmethod
    def reset(cls) -> None:
        """Reset the singleton instance (for testing purposes)."""
        with cls._lock:
            cls._instance = None
            cls._write_mode = None
            cls._consistent_mode = False



async def read_single(
    path: Union[str, BasePath],
    buffer: Optional[ctypes.c_void_p] = None,
    size: Optional[int] = None,
) -> Union[bytes, int]:
    """
    Read the entire contents of a blob.

    This matches boostedblob.read_single() signature with optional zero-copy support.

    Args:
        path: Azure blob path (str or BasePath)
        buffer: Optional pre-allocated buffer for zero-copy read. If provided,
                data is read directly into this buffer without copying.
        size: Size of the buffer in bytes (required if buffer is provided)

    Returns:
        bytes: The blob contents (when buffer is None)
        int: Number of bytes read (when buffer is provided)

    Raises:
        DACSNotFoundError: If the blob does not exist
        DACSStorageError: If read fails
        ValueError: If buffer is provided but size is not, or buffer is too small
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    # Validate buffer/size arguments
    if buffer is not None and size is None:
        raise ValueError("size is required when buffer is provided")

    # Get file size first
    try:
        file_size = await client.get_file_size(converted_path)
    except DACSError:
        raise
    except Exception as e:
        raise handle_native_error(e, operation="get_file_size", path=path_str) from e

    # Handle empty file
    if file_size == 0:
        return 0 if buffer is not None else b""

    # Zero-copy path: read directly into user's buffer
    if buffer is not None:
        if size < file_size:
            raise ValueError(f"Buffer size {size} is smaller than file size {file_size}")
        
        try:
            bytes_read = await client.read_blob(converted_path, buffer, file_size, 0)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="read", path=path_str) from e
        
        return bytes_read

    # Standard path: allocate buffer and copy to bytes
    internal_buffer = alloc_buffer(file_size)
    try:
        try:
            bytes_read = await client.read_blob(converted_path, internal_buffer, file_size, 0)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="read", path=path_str) from e

        # Copy data to Python bytes - maintains API parity with native boostedblob
        data = safe_string_at(internal_buffer, bytes_read)
        return data
    finally:
        release_buffer(internal_buffer)


async def read_byte_range(
    path: Union[str, BasePath],
    byte_range: OptByteRange,
    buffer: Optional[ctypes.c_void_p] = None,
) -> Union[bytes, int]:
    """
    Read a byte range from a blob.

    This matches boostedblob.read_byte_range() signature with optional zero-copy support.

    Args:
        path: Azure blob path (str or BasePath)
        byte_range: Tuple of (start, end) where either can be None.
                   (None, None) reads entire file (start=0, end=EOF)
                   (start, None) reads from start to end of file
                   (None, end) reads first 'end' bytes (from 0 to end)
                   (start, end) reads from start to end (exclusive)
        buffer: Optional pre-allocated buffer for zero-copy read. If provided,
                data is read directly into this buffer without memcpy.
                Buffer must be large enough for the requested range.

    Returns:
        bytes: The requested byte range (when buffer is None)
        int: Number of bytes read (when buffer is provided)

    Raises:
        DACSNotFoundError: If the blob does not exist
        DACSStorageError: If read fails
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)
    start, end = byte_range

    # Get file size if needed for calculating missing bounds
    file_size = None
    if start is None or end is None:
        file_size = await _get_file_size_internal(converted_path, path_str)

    # Calculate actual start and end (None means 0 for start, EOF for end)
    actual_start = 0 if start is None else start
    actual_end = file_size if end is None else end

    # Validate range
    if actual_start < 0:
        raise ValueError(f"start must be >= 0, got {actual_start}")
    if actual_start >= actual_end:
        return b"" if buffer is None else 0  # Empty range

    size = actual_end - actual_start

    # Zero-copy path: use user-provided buffer
    if buffer is not None:
        try:
            bytes_read = await client.read_blob(converted_path, buffer, size, actual_start)
            return bytes_read
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="read_byte_range", path=path_str) from e

    # Standard path: allocate buffer and copy to bytes
    internal_buffer = alloc_buffer(size)
    try:
        try:
            bytes_read = await client.read_blob(converted_path, internal_buffer, size, actual_start)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="read_byte_range", path=path_str) from e

        # Copy data to Python bytes - maintains API parity with native boostedblob
        data = safe_string_at(internal_buffer, bytes_read)
        return data
    finally:
        release_buffer(internal_buffer)


async def _get_file_size_internal(converted_path: str, original_path: str) -> int:
    """Internal helper to get file size."""
    client = _ClientManager.get_instance()
    try:
        return await client.get_file_size(converted_path)
    except DACSError:
        raise
    except Exception as e:
        raise handle_native_error(e, operation="get_file_size", path=original_path) from e


async def write_single(
    path: Union[str, BasePath],
    data: Union[bytes, bytearray, memoryview, str, None] = None,
    overwrite: bool = False,
    buffer: Optional[ctypes.c_void_p] = None,
    size: Optional[int] = None,
) -> None:
    """
    Write data to a blob.

    This matches boostedblob.write_single() signature with optional zero-copy support.

    In efficient mode (default), uses O_EXCL — creating new files only.
    In consistent mode, overwrite=True uses O_TRUNC, overwrite=False uses O_EXCL.

    Args:
        path: Azure blob path (str or BasePath)
        data: Data to write (bytes, bytearray, memoryview, or str). Not needed if buffer is provided.
        overwrite: If True, overwrite existing file (requires consistent mode).
        buffer: Optional pre-allocated buffer for zero-copy write. If provided,
                data is written directly from this buffer without copying.
        size: Size of data in the buffer (required if buffer is provided)

    Raises:
        OperationNotSupportedError: If overwrite=True in efficient mode
        DACSExistsError: If file already exists and overwrite=False in efficient mode
        DACSStorageError: If write fails
        ValueError: If neither data nor buffer is provided, or buffer without size
    """
    client = _ClientManager.get_instance()

    if overwrite and not client.consistent_mode:
        raise OperationNotSupportedError(
            "overwrite=True is not supported in efficient mode. "
            "Enable consistent mode for overwrite support."
        )

    # Validate arguments
    if buffer is not None and size is None:
        raise ValueError("size is required when buffer is provided")
    if buffer is None and data is None:
        raise ValueError("Either data or buffer must be provided")

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    # Zero-copy path: write directly from user's buffer
    if buffer is not None:
        try:
            await client.write_blob(converted_path, buffer, size, overwrite)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="write", path=path_str) from e
        return

    # Standard path: convert data and allocate buffer
    # Convert to bytes if needed
    if isinstance(data, str):
        data_bytes = data.encode("utf-8")
    elif isinstance(data, memoryview):
        data_bytes = bytes(data)
    elif isinstance(data, bytearray):
        data_bytes = bytes(data)
    else:
        data_bytes = data

    data_size = len(data_bytes)

    # Handle empty write
    if data_size == 0:
        # Still need to create the file
        internal_buffer = alloc_buffer(1)  # Minimum allocation
        try:
            try:
                await client.write_blob(converted_path, internal_buffer, 0, overwrite)
            except DACSError:
                raise
            except Exception as e:
                raise handle_native_error(e, operation="write", path=path_str) from e
        finally:
            release_buffer(internal_buffer)
        return

    # Allocate buffer and copy data
    internal_buffer = alloc_buffer(data_size)
    try:
        ctypes.memmove(internal_buffer, data_bytes, data_size)

        try:
            await client.write_blob(converted_path, internal_buffer, data_size, overwrite)
        except DACSError:
            raise
        except Exception as e:
            raise handle_native_error(e, operation="write", path=path_str) from e
    finally:
        release_buffer(internal_buffer)


async def exists(path: Union[str, BasePath]) -> bool:
    """
    Check if a blob exists.

    This matches boostedblob.exists() signature.

    Args:
        path: Azure blob path (str or BasePath)

    Returns:
        bool: True if the blob exists, False otherwise
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    try:
        await client.get_file_stat(converted_path)
        return True
    except OSError as e:
        if e.errno == errno.ENOENT:
            return False
        raise
    except DACSNotFoundError:
        return False


async def stat(path: Union[str, BasePath]) -> Stat:
    """
    Get blob metadata.

    This matches boostedblob.stat() signature.

    Note: md5 and version are not available via DACSCacheClient
    and will always be None.

    Args:
        path: Azure blob path (str or BasePath)

    Returns:
        Stat: Blob metadata

    Raises:
        DACSNotFoundError: If the blob does not exist
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    try:
        stat_result = await client.get_file_stat(converted_path)
    except DACSError:
        raise
    except Exception as e:
        raise handle_native_error(e, operation="stat", path=path_str) from e

    return Stat(
        size=stat_result.st_size,
        mtime=float(stat_result.st_mtime),
        ctime=float(stat_result.st_ctime),
        md5=None,  # Not available via DACSCacheClient
        version=None,  # Not available via DACSCacheClient
    )


async def getsize(path: Union[str, BasePath]) -> int:
    """
    Get blob size in bytes.

    This matches boostedblob.getsize() signature.

    Args:
        path: Azure blob path (str or BasePath)

    Returns:
        int: Blob size in bytes

    Raises:
        DACSNotFoundError: If the blob does not exist
    """
    s = await stat(path)
    return s.size


def listdir(path: Union[str, BasePath]) -> AsyncIterator[BasePath]:
    """
    List contents of a blob directory.

    This matches boostedblob.listdir() signature - returns AsyncIterator of BasePath.

    Args:
        path: Azure blob directory path (str or BasePath)

    Yields:
        BasePath: Path for each entry in the directory

    Raises:
        DACSStorageError: If listing fails
    """
    return _listdir_impl(path)


async def _listdir_impl(path: Union[str, BasePath]) -> AsyncIterator[BasePath]:
    """Internal implementation of listdir."""
    # Handle BasePath input
    if isinstance(path, BasePath):
        azure_path = path if isinstance(path, AzurePath) else AzurePath.from_str(str(path))
        path_str = str(path)
    else:
        azure_path = AzurePath.from_str(path)
        path_str = path

    async for entry in scandir(path_str):
        yield entry.path


def scandir(path: Union[str, BasePath]) -> AsyncIterator[DirEntry]:
    """
    Scan a blob directory, yielding DirEntry objects.

    This matches boostedblob.scandir() signature.

    Args:
        path: Azure blob directory path (str or BasePath)

    Yields:
        DirEntry: Directory entry with metadata

    Raises:
        DACSStorageError: If scanning fails
    """
    return _scandir_impl(path)


async def _scandir_impl(path: Union[str, BasePath]) -> AsyncIterator[DirEntry]:
    """Internal implementation of scandir."""
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        azure_path = path if isinstance(path, AzurePath) else AzurePath.from_str(str(path))
        path_str = str(path)
    else:
        azure_path = AzurePath.from_str(path)
        path_str = path

    converted_path = normalize_azure_path(path_str)
    dirpath = azure_path.ensure_directory_like()

    try:
        async for entry in client.list_dir(converted_path):
            if "d_name" not in entry:
                continue

            name = entry["d_name"]
            # Skip . and .. entries
            if name in (".", ".."):
                continue

            # Determine if directory based on d_type (4 = directory, 8 = file)
            d_type = entry.get("d_type", 0)
            is_dir_entry = d_type == 4
            is_file_entry = d_type == 8

            # Build the entry path
            entry_path = azure_path / name
            if is_dir_entry:
                entry_path = entry_path.ensure_directory_like()

            # Build stat if we have the metadata
            entry_stat = None
            if "d_size" in entry and is_file_entry:
                # Fetch proper ctime using get_file_stat API
                entry_converted_path = normalize_azure_path(str(entry_path))
                try:
                    file_stat = await client.get_file_stat(entry_converted_path)
                    ctime = float(file_stat.st_ctime)
                except Exception:
                    # Fallback to mtime if get_file_stat fails
                    ctime = float(entry.get("d_mtime", 0))

                entry_stat = Stat(
                    size=entry.get("d_size", 0),
                    mtime=float(entry.get("d_mtime", 0)),
                    ctime=ctime,
                    md5=None,
                    version=None,
                )

            if is_dir_entry:
                yield DirEntry.from_dirpath(entry_path)
            else:
                yield DirEntry.from_path_stat(entry_path, entry_stat) if entry_stat else DirEntry(
                    path=entry_path, is_dir=False, is_file=True, stat=None
                )
    except DACSError:
        raise
    except Exception as e:
        raise handle_native_error(e, operation="scandir", path=path_str) from e


def listtree(path: Union[str, BasePath]) -> AsyncIterator[BasePath]:
    """
    List all files in the directory tree rooted at path.

    This matches boostedblob.listtree() signature.

    Args:
        path: Azure blob directory path (str or BasePath)

    Yields:
        BasePath: Path for each file in the tree

    Raises:
        DACSStorageError: If listing fails
    """
    return _listtree_impl(path)


async def _listtree_impl(path: Union[str, BasePath]) -> AsyncIterator[BasePath]:
    """Internal implementation of listtree."""
    async for entry in scantree(path):
        yield entry.path


def scantree(path: Union[str, BasePath]) -> AsyncIterator[DirEntry]:
    """
    Scan all files in the directory tree rooted at path.

    This matches boostedblob.scantree() signature.

    Args:
        path: Azure blob directory path (str or BasePath)

    Yields:
        DirEntry: Directory entry for each file in the tree

    Raises:
        DACSStorageError: If scanning fails
    """
    return _scantree_impl(path)


async def _scantree_impl(path: Union[str, BasePath]) -> AsyncIterator[DirEntry]:
    """Internal implementation of scantree - recursively scan directory tree."""
    async for entry in scandir(path):
        if entry.is_file:
            yield entry
        elif entry.is_dir:
            # Recursively scan subdirectory
            async for sub_entry in scantree(entry.path):
                yield sub_entry


async def isdir(path: Union[str, BasePath]) -> bool:
    """
    Check if path is a directory.

    This matches boostedblob.isdir() signature.

    Args:
        path: Azure blob path (str or BasePath)

    Returns:
        bool: True if path is a directory
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    try:
        stat_result = await client.get_file_stat(converted_path)
        return stat_module.S_ISDIR(stat_result.st_mode)
    except (DACSNotFoundError, OSError, FileNotFoundError):
        return False


async def isfile(path: Union[str, BasePath]) -> bool:
    """
    Check if path is a file.

    This matches boostedblob.isfile() signature.

    Args:
        path: Azure blob path (str or BasePath)

    Returns:
        bool: True if path is a file
    """
    client = _ClientManager.get_instance()

    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path

    converted_path = normalize_azure_path(path_str)

    try:
        stat_result = await client.get_file_stat(converted_path)
        return stat_module.S_ISREG(stat_result.st_mode)
    except (DACSNotFoundError, OSError, FileNotFoundError):
        return False


# ==============================
# Streaming Read Functions (implemented using read_byte_range)
# ==============================


def byte_range_to_str(byte_range: OptByteRange) -> Optional[str]:
    """
    Convert a byte range tuple to HTTP Range header string format.
    
    This matches boostedblob.read.byte_range_to_str() signature.
    
    Args:
        byte_range: Tuple of (start, end) where either can be None
        
    Returns:
        str: HTTP Range header format string, or None for full file
    """
    start, end = byte_range
    if start is not None and end is not None:
        return f"bytes={start}-{end-1}"
    if start is not None:
        return f"bytes={start}-"
    if end is not None:
        if end > 0:
            return f"bytes=0-{end-1}"
        return f"bytes=-{-int(end)}"
    return None


async def read_stream(
    path: Union[str, BasePath],
    executor: Any = None,
    size: Optional[int] = None,
) -> AsyncIterator[bytes]:
    """
    Read the content of path as a stream of chunks.
    
    This matches boostedblob.read.read_stream() signature.
    
    Note: The executor parameter is accepted for API compatibility but ignored.
    DACSCacheClient doesn't use BoostExecutor for parallelism.
    
    Args:
        path: Azure blob path (str or BasePath)
        executor: Ignored (for API compatibility with boostedblob)
        size: If specified, saves a network call to get file size
        
    Yields:
        bytes: Chunks of data (DEFAULT_CHUNK_SIZE each, except possibly the last)
        
    Raises:
        DACSNotFoundError: If the blob does not exist
    """
    return _read_stream_impl(path, size)


async def _read_stream_impl(
    path: Union[str, BasePath],
    size: Optional[int] = None,
) -> AsyncIterator[bytes]:
    """Internal implementation of read_stream."""
    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path
    
    # Get file size if not provided
    if size is None:
        size = await getsize(path_str)
    
    # Handle empty file
    if size == 0:
        return
    
    # Generate byte ranges and yield chunks
    for start in range(0, size, DEFAULT_CHUNK_SIZE):
        end = min(start + DEFAULT_CHUNK_SIZE, size)
        chunk = await read_byte_range(path_str, (start, end))
        yield chunk


async def read_stream_unordered(
    path: Union[str, BasePath],
    executor: Any = None,
    size: Optional[int] = None,
) -> AsyncIterator[Tuple[bytes, ByteRange]]:
    """
    Read the content of path as an unordered stream of chunks with byte ranges.
    
    This matches boostedblob.read.read_stream_unordered() signature.
    
    Note: In this implementation, chunks are returned in order since we don't
    have parallel download capability. The executor parameter is ignored.
    
    Args:
        path: Azure blob path (str or BasePath)
        executor: Ignored (for API compatibility with boostedblob)
        size: If specified, saves a network call to get file size
        
    Yields:
        tuple[bytes, ByteRange]: Tuples of (chunk_data, (start_offset, end_offset))
        
    Raises:
        DACSNotFoundError: If the blob does not exist
    """
    return _read_stream_unordered_impl(path, size)


async def _read_stream_unordered_impl(
    path: Union[str, BasePath],
    size: Optional[int] = None,
) -> AsyncIterator[Tuple[bytes, ByteRange]]:
    """Internal implementation of read_stream_unordered."""
    # Handle BasePath input
    if isinstance(path, BasePath):
        path_str = str(path)
    else:
        path_str = path
    
    # Get file size if not provided
    if size is None:
        size = await getsize(path_str)
    
    # Handle empty file
    if size == 0:
        return
    
    # Generate byte ranges and yield chunks with their ranges
    for start in range(0, size, DEFAULT_CHUNK_SIZE):
        end = min(start + DEFAULT_CHUNK_SIZE, size)
        chunk = await read_byte_range(path_str, (start, end))
        yield (chunk, (start, end))


async def read_chunked(
    path: Union[str, BasePath],
    executor: Any = None,
    size: Optional[int] = None,
) -> bytes:
    """
    Read the content of path using chunked streaming.
    
    This matches boostedblob.read.read_chunked() signature.
    
    Note: The executor parameter is accepted for API compatibility but ignored.
    
    Args:
        path: Azure blob path (str or BasePath)
        executor: Ignored (for API compatibility with boostedblob)
        size: If specified, saves a network call to get file size
        
    Returns:
        bytes: The complete file contents
        
    Raises:
        DACSNotFoundError: If the blob does not exist
    """
    chunks = []
    async for chunk in _read_stream_impl(path, size):
        chunks.append(chunk)
    return b"".join(chunks)


# ==============================
# Unsupported Write Streaming Operations
# ==============================


async def write_stream(
    path: Union[str, BasePath],
    stream: Any = None,
    executor: Any = None,
    overwrite: bool = False,
) -> None:
    """
    Not supported - DACSCacheClient doesn't support streaming/chunked writes.
    
    Use write_single() instead for writing complete data.
    """
    raise OperationNotSupportedError(
        "write_stream() is not supported by DACSCacheClient. "
        "DACSCacheClient uses O_EXCL flag and doesn't support append/streaming writes. "
        "Use write_single() instead."
    )


async def write_stream_unordered(
    path: Union[str, BasePath],
    stream: Any = None,
    executor: Any = None,
    overwrite: bool = False,
) -> None:
    """
    Not supported - DACSCacheClient doesn't support streaming/chunked writes.
    
    Use write_single() instead for writing complete data.
    """
    raise OperationNotSupportedError(
        "write_stream_unordered() is not supported by DACSCacheClient. "
        "DACSCacheClient uses O_EXCL flag and doesn't support append/streaming writes. "
        "Use write_single() instead."
    )


async def write_chunked(
    path: Union[str, BasePath],
    data: Union[bytes, bytearray, memoryview],
    executor: Any = None,
    overwrite: bool = False,
) -> None:
    """
    Not supported - DACSCacheClient doesn't support streaming/chunked writes.
    
    Use write_single() instead for writing complete data.
    """
    raise OperationNotSupportedError(
        "write_chunked() is not supported by DACSCacheClient. "
        "DACSCacheClient uses O_EXCL flag and doesn't support append/streaming writes. "
        "Use write_single() instead."
    )


# ==============================
# Unsupported Listing Operations
# ==============================


def list_blobs(
    path: Union[str, BasePath],
    delimiter: Optional[str] = None,
    allow_prefix: bool = False,
) -> AsyncIterator[DirEntry]:
    """
    Not supported - use scandir() or scantree() instead.
    
    list_blobs() is an internal boostedblob function used by scandir/scantree.
    """
    raise OperationNotSupportedError(
        "list_blobs() is not supported by DACSCacheClient. "
        "Use scandir() for directory listing or scantree() for recursive listing."
    )


def glob_scandir(path: Union[str, BasePath]) -> AsyncIterator[DirEntry]:
    """
    Not supported - glob patterns are not supported.
    
    Use scandir() with manual filtering instead.
    """
    raise OperationNotSupportedError(
        "glob_scandir() is not supported by DACSCacheClient. "
        "Use scandir() with manual filtering instead."
    )


# ==============================
# Unsupported Delete Operations
# ==============================


async def remove(path: Union[str, BasePath]) -> BasePath:
    """Not supported - DACSCacheClient is read-focused."""
    raise OperationNotSupportedError(
        "remove() is not supported by DACSCacheClient. "
        "DACSCacheClient is designed for read-heavy workloads."
    )


async def rmtree(path: Union[str, BasePath], executor: Any = None) -> None:
    """Not supported - DACSCacheClient is read-focused."""
    raise OperationNotSupportedError(
        "rmtree() is not supported by DACSCacheClient. "
        "DACSCacheClient is designed for read-heavy workloads."
    )


async def rmtree_iterator(
    path: Union[str, BasePath],
    executor: Any = None,
) -> AsyncIterator[BasePath]:
    """Not supported - DACSCacheClient is read-focused."""
    raise OperationNotSupportedError(
        "rmtree_iterator() is not supported by DACSCacheClient."
    )
    # This yield is never reached but makes the function a proper async generator
    yield  # type: ignore


async def glob_remove(
    path: Union[str, BasePath],
    executor: Any = None,
) -> AsyncIterator[BasePath]:
    """Not supported - DACSCacheClient is read-focused."""
    raise OperationNotSupportedError(
        "glob_remove() is not supported by DACSCacheClient."
    )
    yield  # type: ignore


# ==============================
# Unsupported Copy Operations
# ==============================


async def copyfile(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    executor: Any = None,
    overwrite: bool = False,
    size: Optional[int] = None,
) -> None:
    """Not supported - DACSCacheClient doesn't support copy operations."""
    raise OperationNotSupportedError(
        "copyfile() is not supported by DACSCacheClient."
    )


async def copytree(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    executor: Any = None,
) -> None:
    """Not supported - DACSCacheClient doesn't support copy operations."""
    raise OperationNotSupportedError(
        "copytree() is not supported by DACSCacheClient."
    )


async def copytree_iterator(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    executor: Any = None,
) -> AsyncIterator[BasePath]:
    """Not supported - DACSCacheClient doesn't support copy operations."""
    raise OperationNotSupportedError(
        "copytree_iterator() is not supported by DACSCacheClient."
    )
    yield  # type: ignore


async def copyglob_iterator(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    executor: Any = None,
) -> AsyncIterator[BasePath]:
    """Not supported - DACSCacheClient doesn't support copy operations."""
    raise OperationNotSupportedError(
        "copyglob_iterator() is not supported by DACSCacheClient."
    )
    yield  # type: ignore


# ==============================
# Unsupported Sync Operations
# ==============================


async def sync(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    executor: Any = None,
    delete: bool = False,
    exclude: Optional[str] = None,
) -> AsyncIterator[BasePath]:
    """Not supported - DACSCacheClient doesn't support sync operations."""
    raise OperationNotSupportedError(
        "sync() is not supported by DACSCacheClient."
    )
    yield  # type: ignore


async def sync_action_iterator(
    src: Union[str, BasePath],
    dst: Union[str, BasePath],
    exclude: Optional[str] = None,
) -> Iterator[Any]:
    """Not supported - DACSCacheClient doesn't support sync operations."""
    raise OperationNotSupportedError(
        "sync_action_iterator() is not supported by DACSCacheClient."
    )


# ==============================
# Unsupported Share Operations
# ==============================


async def get_url(path: Union[str, BasePath]) -> Tuple[str, Optional[datetime.datetime]]:
    """
    Not supported - generating signed URLs requires Azure SAS token generation
    which is outside DACSCacheClient's scope.
    """
    raise OperationNotSupportedError(
        "get_url() is not supported by DACSCacheClient. "
        "Generating signed URLs requires Azure SAS token generation."
    )


# ==============================
# Session Management (API compatibility)
# ==============================


def ensure_session(fn: F) -> F:
    """
    Decorator for session management (API compatibility with boostedblob).
    
    DACSCacheClient manages its own sessions via _ClientManager,
    so this decorator is a no-op passthrough.
    """
    return fn


class BoostExecutor:
    """
    Placeholder BoostExecutor for API compatibility.
    
    DACSCacheClient doesn't use BoostExecutor for parallelism.
    This class is provided so code that creates a BoostExecutor doesn't fail,
    but the executor parameter is ignored in all adapter functions.
    
    For full BoostExecutor functionality, use native boostedblob:
        from boostedblob import BoostExecutor
    """
    
    def __init__(self, concurrency: int = 8) -> None:
        self.concurrency = concurrency
    
    async def __aenter__(self) -> "BoostExecutor":
        return self
    
    async def __aexit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        pass


# ==============================
# Shutdown
# ==============================


async def shutdown() -> None:
    """
    Shutdown the DACSCacheClient and clean up resources.

    This should be called before terminating the application to ensure:
    - All open file handles are properly closed
    - Metrics are logged
    - Resources are cleaned up
    """
    client = _ClientManager.get_instance()
    await client.shutdown()


# ==============================
# Synthetic Submodules (for boostedblob API parity)
# ==============================
# These create module-like namespaces so users can access functions via:
#   bbb.read.read_single(path)
#   bbb.write.write_single(path, data)
#   bbb.share.get_url(path)


# Create synthetic 'read' submodule
read = types.ModuleType("dacs_boostedblob_adapter.read")
read.__doc__ = "Read operations for DACSCacheClient (boostedblob API compatibility)"
read.read_single = read_single
read.read_byte_range = read_byte_range
read.read_stream = read_stream
read.read_stream_unordered = read_stream_unordered
read.read_chunked = read_chunked
read.byte_range_to_str = byte_range_to_str
read.ByteRange = ByteRange
read.OptByteRange = OptByteRange
sys.modules["dacs_boostedblob_adapter.read"] = read

# Create synthetic 'write' submodule
write = types.ModuleType("dacs_boostedblob_adapter.write")
write.__doc__ = "Write operations for DACSCacheClient (boostedblob API compatibility)"
write.write_single = write_single
write.write_stream = write_stream
write.write_stream_unordered = write_stream_unordered
write.write_chunked = write_chunked
sys.modules["dacs_boostedblob_adapter.write"] = write

# Create synthetic 'share' submodule
share = types.ModuleType("dacs_boostedblob_adapter.share")
share.__doc__ = "Share operations for DACSCacheClient (boostedblob API compatibility)"
share.get_url = get_url
sys.modules["dacs_boostedblob_adapter.share"] = share
