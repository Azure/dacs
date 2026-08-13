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

# ---------------------------------------------------------

__version__ = "0.9.0-PLACEHOLDER"

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
from py_dacs_client.dacs_utils import WriteMode

__all__ = [
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
    "handle_native_error",
    "check_native_result",
    "errno_to_exception_class",
]
