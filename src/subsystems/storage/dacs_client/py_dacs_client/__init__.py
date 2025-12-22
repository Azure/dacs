# ---------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# ---------------------------------------------------------

__path__ = __import__("pkgutil").extend_path(__path__, __name__)  # type: ignore

# Export common exceptions for easy access
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

__all__ = [
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
    "handle_native_error",
    "check_native_result",
    "errno_to_exception_class",
]
