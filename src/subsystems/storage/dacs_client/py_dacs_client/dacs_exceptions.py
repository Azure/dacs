#!/usr/bin/env python3
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

# DACS Client - Exception Handling Module
# --------------------------------------------------------------------------------------------------------------------
# This module provides common exception classes and utilities for handling errors from the native
# storage library. It wraps native C++ exceptions and provides meaningful Python exceptions.
#
# Exception Hierarchy:
#   DACSError (base)
#   ├── DACSStorageError - Storage operation failures (read/write/open)
#   │   ├── DACSAuthenticationError - Authentication/authorization failures (EPERM)
#   │   ├── DACSPermissionError - Access denied errors (EACCES)
#   │   ├── DACSNotFoundError - Blob/container not found (ENOENT)
#   │   ├── DACSExistsError - Resource already exists (EEXIST)
#   │   ├── DACSBusyError - Resource busy/locked (EBUSY)
#   │   ├── DACSTimeoutError - Operation timed out (ETIMEDOUT)
#   │   ├── DACSNetworkError - Network connectivity issues (ECONNREFUSED, etc.)
#   │   ├── DACSQuotaError - Quota exceeded (EDQUOT, EFBIG)
#   │   ├── DACSInvalidInputError - Invalid parameters (EINVAL)
#   │   └── DACSServiceUnavailableError - Service temporarily unavailable (EAGAIN)
#   └── OperationNotSupportedError - Unsupported operations (ENOTSUP)
#
# Usage:
#   from dacs_exceptions import DACSStorageError, handle_native_error
#
#   try:
#       result = native_call()
#   except Exception as e:
#       raise handle_native_error(e, operation="read", path=blob_path)
# --------------------------------------------------------------------------------------------------------------------

import ctypes
import errno
import logging
import os
import re
from typing import Optional, Type

logger = logging.getLogger(__name__)


class DACSError(Exception):
    """Base exception for all DACS Client errors."""

    pass


class OperationNotSupportedError(DACSError):
    """Exception raised when an operation is not supported by DACS Client (ENOTSUP)."""

    pass


class DACSStorageError(DACSError):
    """Exception raised when a storage operation fails in the native library.

    This exception wraps errors from the native C++ storage library, including:
    - Authentication/authorization errors (AuthorizationPermissionMismatch)
    - Network errors (connection failures, timeouts)
    - Storage errors (blob not found, container not found)
    - Internal errors (cache errors, remote client errors)

    Attributes:
        message: Human-readable error message
        operation: The operation that failed (e.g., "read", "write", "open")
        path: The blob path involved in the operation
        error_code: Native error code if available
        errno_code: Mapped errno code if available
        request_id: Azure request ID if available (for debugging with Azure support)
    """

    def __init__(
        self,
        message: str,
        operation: Optional[str] = None,
        path: Optional[str] = None,
        error_code: Optional[int] = None,
        errno_code: Optional[int] = None,
        request_id: Optional[str] = None,
        cause: Optional[Exception] = None,
    ):
        self.message = message
        self.operation = operation
        self.path = path
        self.error_code = error_code
        self.errno_code = errno_code
        self.request_id = request_id
        self.cause = cause

        # Build detailed error message
        parts = [message]
        if operation:
            parts.append(f"Operation: {operation}")
        if path:
            parts.append(f"Path: {path}")
        if error_code is not None:
            parts.append(f"Error code: {error_code}")
        if errno_code is not None:
            parts.append(f"errno: {errno_code} ({os.strerror(errno_code)})")
        if request_id:
            parts.append(f"RequestId: {request_id}")

        super().__init__(" | ".join(parts))


class DACSAuthenticationError(DACSStorageError):
    """Exception raised when authentication or authorization fails (EPERM).

    Common causes:
    - Invalid or expired SAS token
    - Managed identity not configured correctly
    - Missing permissions on the storage account
    - AZURE_CLIENT_ID not set or incorrect
    - AuthorizationPermissionMismatch
    - AuthorizationFailed
    - InsufficientAccountPermissions
    - AccountIsDisabled
    """

    pass


class DACSPermissionError(DACSStorageError):
    """Exception raised when access is denied (EACCES).

    Common causes:
    - AccessDenied response from Azure
    - Forbidden (403) responses
    - Insufficient permissions for the specific operation
    """

    pass


class DACSNotFoundError(DACSStorageError):
    """Exception raised when a blob or container is not found (ENOENT).

    Common causes:
    - Blob path is incorrect
    - Container doesn't exist
    - Storage account doesn't exist
    - BlobNotFound, ContainerNotFound, ResourceNotFound
    - 404 responses
    """

    pass


class DACSExistsError(DACSStorageError):
    """Exception raised when a resource already exists (EEXIST).

    Common causes:
    - BlobAlreadyExists
    - ContainerAlreadyExists
    - ResourceAlreadyExists
    - 409 Conflict responses
    """

    pass


class DACSBusyError(DACSStorageError):
    """Exception raised when a resource is busy or locked (EBUSY).

    Common causes:
    - LeaseAlreadyPresent
    - LeaseIdMissing
    - LeaseConflict
    - OperationNotAllowedOnRootBlob
    """

    pass


class DACSTimeoutError(DACSStorageError):
    """Exception raised when an operation times out (ETIMEDOUT).

    Common causes:
    - ServerTimeout
    - ClientTimeout
    - OperationTimedOut
    - Network latency issues
    """

    pass


class DACSNetworkError(DACSStorageError):
    """Exception raised when a network operation fails.

    Common causes:
    - Network connectivity issues (ENETUNREACH)
    - DNS resolution failures
    - Connection refused (ECONNREFUSED)
    - Connection reset (ECONNRESET)
    - Host unreachable (EHOSTUNREACH)
    - SSL/TLS errors
    """

    pass


class DACSQuotaError(DACSStorageError):
    """Exception raised when quota is exceeded (EDQUOT, EFBIG).

    Common causes:
    - QuotaExceeded
    - AccountQuotaExceeded
    - ShareQuotaExceeded
    - MaxBlobSizeExceeded
    - BlockCountExceeded
    - FileTooLarge
    """

    pass


class DACSInvalidInputError(DACSStorageError):
    """Exception raised when input parameters are invalid (EINVAL).

    Common causes:
    - InvalidInput
    - InvalidParameter
    - InvalidRange
    - InvalidHeaderValue
    - InvalidQueryParameter
    - OutOfRangeInput
    - 400 Bad Request responses
    """

    pass


class DACSServiceUnavailableError(DACSStorageError):
    """Exception raised when the service is temporarily unavailable (EAGAIN).

    This is typically a transient error that may be retried.

    Common causes:
    - ServiceUnavailable
    - ServerBusy
    - InternalError
    - 500/503 responses
    """

    pass


class DACSIsDirectoryError(DACSStorageError):
    """Exception raised when a file operation is attempted on a directory (EISDIR)."""

    pass


class DACSNotDirectoryError(DACSStorageError):
    """Exception raised when a directory operation is attempted on a file (ENOTDIR)."""

    pass


class DACSNotEmptyError(DACSStorageError):
    """Exception raised when trying to delete a non-empty directory (ENOTEMPTY)."""

    pass


class DACSNameTooLongError(DACSStorageError):
    """Exception raised when a path/name is too long (ENAMETOOLONG)."""

    pass


class DACSReadOnlyError(DACSStorageError):
    """Exception raised when writing to a read-only resource (EROFS)."""

    pass


# Patterns to match common Azure storage error messages
# These match the error mappings in StorageInterceptor.cpp mapExceptionToErrno()

# EPERM - Authentication/Authorization errors
_AUTH_ERROR_PATTERNS = [
    r"AuthorizationPermissionMismatch",
    r"AuthorizationFailed",
    r"AuthorizationFailure",
    r"InsufficientAccountPermissions",
    r"AccountIsDisabled",
]

# EACCES - Access denied errors
_ACCESS_DENIED_PATTERNS = [
    r"AccessDenied",
    r"Forbidden",
    r"\b403\b",
]

# ENOENT - Not found errors
_NOT_FOUND_PATTERNS = [
    r"BlobNotFound",
    r"ContainerNotFound",
    r"ResourceNotFound",
    r"PathNotFound",
    r"FileNotFound",
    r"\b404\b",
    r"NotFound",
    r"The specified blob does not exist",
    r"The specified container does not exist",
]

# EEXIST - Already exists errors
_EXISTS_PATTERNS = [
    r"BlobAlreadyExists",
    r"ContainerAlreadyExists",
    r"ResourceAlreadyExists",
    r"PathAlreadyExists",
    r"\b409\b",
]

# EBUSY - Resource busy/locked errors
_BUSY_PATTERNS = [
    r"LeaseAlreadyPresent",
    r"LeaseIdMissing",
    r"LeaseConflict",
    r"OperationNotAllowedOnRootBlob",
]

# ETIMEDOUT - Timeout errors
_TIMEOUT_PATTERNS = [
    r"Timeout",
    r"OperationTimedOut",
    r"ServerTimeout",
    r"ClientTimeout",
]

# Network errors (ECONNREFUSED, ENETUNREACH, EHOSTUNREACH, ECONNRESET)
_NETWORK_ERROR_PATTERNS = [
    r"ConnectionRefused",
    r"ConnectionReset",
    r"Connection refused",
    r"NetworkUnreachable",
    r"Network is unreachable",
    r"HostUnreachable",
    r"No route to host",
    r"ConnectionError",
    r"SocketError",
    r"NetworkError",
    r"DNSError",
    r"SSLError",
]

# EAGAIN - Service unavailable (retryable)
_SERVICE_UNAVAILABLE_PATTERNS = [
    r"ServiceUnavailable",
    r"ServerBusy",
    r"\b503\b",
    r"InternalError",
    r"\b500\b",
]

# EDQUOT/EFBIG - Quota/size errors
_QUOTA_PATTERNS = [
    r"QuotaExceeded",
    r"AccountQuotaExceeded",
    r"ShareQuotaExceeded",
    r"MaxBlobSizeExceeded",
    r"BlockCountExceeded",
    r"FileTooLarge",
]

# EINVAL - Invalid input errors
_INVALID_INPUT_PATTERNS = [
    r"InvalidInput",
    r"InvalidParameter",
    r"InvalidRange",
    r"InvalidHeaderValue",
    r"InvalidQueryParameter",
    r"OutOfRangeInput",
    r"\b400\b",
]

# ENAMETOOLONG - Name too long
_NAME_TOO_LONG_PATTERNS = [
    r"InvalidBlobName",
    r"InvalidContainerName",
    r"NameTooLong",
]

# EROFS - Read-only
_READ_ONLY_PATTERNS = [
    r"ReadOnlyAttribute",
    r"ConditionNotMet",
]

# ENOTSUP - Unsupported operation
_UNSUPPORTED_PATTERNS = [
    r"UnsupportedOperation",
    r"NotImplemented",
    r"FeatureNotSupported",
    r"\b501\b",
]

# EISDIR - Is a directory
_IS_DIRECTORY_PATTERNS = [
    r"PathIsDirectory",
    r"IsADirectory",
]

# ENOTDIR - Not a directory
_NOT_DIRECTORY_PATTERNS = [
    r"PathIsNotDirectory",
    r"NotADirectory",
    r"ParentNotFound",
]

# ENOTEMPTY - Directory not empty
_NOT_EMPTY_PATTERNS = [
    r"DirectoryNotEmpty",
]


def _extract_request_id(error_message: str) -> Optional[str]:
    """Extract Azure RequestId from error message if present."""
    match = re.search(r"RequestId:([a-f0-9-]+)", error_message, re.IGNORECASE)
    if match:
        return match.group(1)
    return None


def _match_patterns(error_message: str, patterns: list) -> bool:
    """Check if error message matches any of the given patterns."""
    for pattern in patterns:
        if re.search(pattern, error_message, re.IGNORECASE):
            return True
    return False


def _classify_error(error_message: str) -> tuple[Type[DACSStorageError], Optional[int]]:
    """Classify error based on message patterns.

    Returns a tuple of (exception_class, errno_code) matching the logic in
    StorageInterceptor.cpp mapExceptionToErrno().
    """
    # Check patterns in order of specificity (most specific first)

    # EPERM - Authentication/Authorization errors
    if _match_patterns(error_message, _AUTH_ERROR_PATTERNS):
        return DACSAuthenticationError, errno.EPERM

    # EACCES - Access denied
    if _match_patterns(error_message, _ACCESS_DENIED_PATTERNS):
        return DACSPermissionError, errno.EACCES

    # ENOENT - Not found
    if _match_patterns(error_message, _NOT_FOUND_PATTERNS):
        return DACSNotFoundError, errno.ENOENT

    # EEXIST - Already exists
    if _match_patterns(error_message, _EXISTS_PATTERNS):
        return DACSExistsError, errno.EEXIST

    # EBUSY - Resource busy
    if _match_patterns(error_message, _BUSY_PATTERNS):
        return DACSBusyError, errno.EBUSY

    # ETIMEDOUT - Timeout
    if _match_patterns(error_message, _TIMEOUT_PATTERNS):
        return DACSTimeoutError, errno.ETIMEDOUT

    # Network errors
    if _match_patterns(error_message, _NETWORK_ERROR_PATTERNS):
        return DACSNetworkError, errno.ECONNREFUSED

    # EAGAIN - Service unavailable (retryable)
    if _match_patterns(error_message, _SERVICE_UNAVAILABLE_PATTERNS):
        return DACSServiceUnavailableError, errno.EAGAIN

    # EDQUOT/EFBIG - Quota errors
    if _match_patterns(error_message, _QUOTA_PATTERNS):
        return DACSQuotaError, errno.EDQUOT

    # EINVAL - Invalid input
    if _match_patterns(error_message, _INVALID_INPUT_PATTERNS):
        return DACSInvalidInputError, errno.EINVAL

    # ENAMETOOLONG - Name too long
    if _match_patterns(error_message, _NAME_TOO_LONG_PATTERNS):
        return DACSNameTooLongError, errno.ENAMETOOLONG

    # EROFS - Read-only
    if _match_patterns(error_message, _READ_ONLY_PATTERNS):
        return DACSReadOnlyError, errno.EROFS

    # ENOTSUP - Unsupported operation
    if _match_patterns(error_message, _UNSUPPORTED_PATTERNS):
        return OperationNotSupportedError, errno.ENOTSUP

    # EISDIR - Is a directory
    if _match_patterns(error_message, _IS_DIRECTORY_PATTERNS):
        return DACSIsDirectoryError, errno.EISDIR

    # ENOTDIR - Not a directory
    if _match_patterns(error_message, _NOT_DIRECTORY_PATTERNS):
        return DACSNotDirectoryError, errno.ENOTDIR

    # ENOTEMPTY - Directory not empty
    if _match_patterns(error_message, _NOT_EMPTY_PATTERNS):
        return DACSNotEmptyError, errno.ENOTEMPTY

    # Default to generic storage error with EIO
    return DACSStorageError, errno.EIO


def errno_to_exception_class(errno_code: int) -> Type[DACSStorageError]:
    """Map an errno code to the appropriate exception class.

    This is useful when the native library has already set errno and we need
    to raise the appropriate Python exception.
    """
    errno_map = {
        errno.EPERM: DACSAuthenticationError,
        errno.EACCES: DACSPermissionError,
        errno.ENOENT: DACSNotFoundError,
        errno.EEXIST: DACSExistsError,
        errno.EBUSY: DACSBusyError,
        errno.ETIMEDOUT: DACSTimeoutError,
        errno.ECONNREFUSED: DACSNetworkError,
        errno.ENETUNREACH: DACSNetworkError,
        errno.EHOSTUNREACH: DACSNetworkError,
        errno.ECONNRESET: DACSNetworkError,
        errno.EAGAIN: DACSServiceUnavailableError,
        errno.EDQUOT: DACSQuotaError,
        errno.EFBIG: DACSQuotaError,
        errno.EINVAL: DACSInvalidInputError,
        errno.ENAMETOOLONG: DACSNameTooLongError,
        errno.EROFS: DACSReadOnlyError,
        errno.ENOTSUP: OperationNotSupportedError,
        errno.EISDIR: DACSIsDirectoryError,
        errno.ENOTDIR: DACSNotDirectoryError,
        errno.ENOTEMPTY: DACSNotEmptyError,
    }
    return errno_map.get(errno_code, DACSStorageError)


def handle_native_error(
    error: Exception, operation: Optional[str] = None, path: Optional[str] = None, error_code: Optional[int] = None
) -> DACSStorageError:
    """Convert a native exception to an appropriate DACSStorageError.

    This function analyzes the error message to determine the specific type
    of storage error and returns an appropriate exception with detailed context.

    Args:
        error: The original exception from the native library
        operation: The operation being performed (e.g., "read", "write", "open")
        path: The blob path involved
        error_code: Native error code if available

    Returns:
        An appropriate DACSStorageError subclass

    Example:
        try:
            bytes_read = native_read(fd, buffer, size)
        except Exception as e:
            raise handle_native_error(e, operation="read", path=blob_path)
    """
    # If already a DACSError, update path if provided and return as-is
    if isinstance(error, DACSError):
        if path is not None and hasattr(error, 'path'):
            error.path = path
        return error

    error_message = str(error)
    request_id = _extract_request_id(error_message)

    # For OSError with a valid errno, use errno directly instead of lossy message pattern matching.
    # The native storage intercept sets errno reliably; message patterns are a fallback for non-OSError.
    if isinstance(error, OSError) and error.errno:
        errno_code = error.errno
        error_class = errno_to_exception_class(errno_code)
    else:
        error_class, errno_code = _classify_error(error_message)

    # For OperationNotSupportedError, use different constructor
    if error_class == OperationNotSupportedError:
        return OperationNotSupportedError(error_message)

    return error_class(
        message=error_message,
        operation=operation,
        path=path,
        error_code=error_code,
        errno_code=errno_code,
        request_id=request_id,
        cause=error,
    )


def check_native_result(result: int, operation: str, path: Optional[str] = None, context: Optional[str] = None) -> int:
    """Check the result of a native library call and raise appropriate exception if failed.

    Args:
        result: The return value from the native call
        operation: The operation name (e.g., "read", "write", "open")
        path: The blob path involved
        context: Additional context for the error message

    Returns:
        The result if successful (>= 0)

    Raises:
        DACSStorageError: If result indicates failure (< 0)

    Example:
        bytes_read = check_native_result(
            native_read(fd, buffer, size),
            operation="read",
            path=blob_path
        )
    """
    if result < 0:
        # Try to get errno for more context
        errno_val = ctypes.get_errno()
        error_message = os.strerror(errno_val) if errno_val else f"Operation failed with code {result}"

        message = f"{operation} failed: {error_message}"
        if context:
            message = f"{message} ({context})"

        # Use the errno to determine the appropriate exception class
        error_class = errno_to_exception_class(errno_val) if errno_val else DACSStorageError

        raise error_class(message=message, operation=operation, path=path, error_code=result, errno_code=errno_val)

    return result


def wrap_native_call(func, operation: str, path: Optional[str] = None):
    """Decorator/wrapper to catch exceptions from native calls.

    This wraps a native library call and converts any exceptions to
    DACSStorageError with appropriate context.

    Args:
        func: The function to call
        operation: Operation name for error context
        path: Blob path for error context

    Returns:
        A wrapper function that catches and converts exceptions

    Example:
        result = wrap_native_call(
            lambda: native_lib.read(fd, buf, size),
            operation="read",
            path=blob_path
        )()
    """

    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except DACSError:
            # Re-raise our own exceptions as-is
            raise
        except OSError as e:
            # Map OSError errno to appropriate exception class
            error_class = errno_to_exception_class(e.errno) if e.errno else DACSStorageError
            raise error_class(
                message=str(e), operation=operation, path=path, error_code=e.errno, errno_code=e.errno, cause=e
            ) from e
        except Exception as e:
            raise handle_native_error(e, operation=operation, path=path) from e

    return wrapper
