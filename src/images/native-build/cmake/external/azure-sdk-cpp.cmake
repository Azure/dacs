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

# Fetch content
#
include(FetchContent)

# Disable Azure SDK features we don't need
set(AZURE_SDK_DISABLE_AUTO_VCPKG ON CACHE BOOL "" FORCE)
set(DISABLE_AZURE_CORE_OPENTELEMETRY ON CACHE BOOL "" FORCE)
set(BUILD_WINDOWS_UWP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_PERFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(AZ_ALL_LIBRARIES ON CACHE BOOL "" FORCE)

# azure-storage-blobs 12.17.0+ provides the Structured Message (XSM/1.0) validation APIs
# used by SdkBlobClient for CRC64 download validation.
FetchContent_Declare(azuresdkforcpp
    # Pin the Azure SDK for C++ storage blobs 12.18.0 tag archive by SHA-256.
    URL      https://github.com/Azure/azure-sdk-for-cpp/archive/refs/tags/azure-storage-blobs_12.18.0.tar.gz
    URL_HASH SHA256=0f266f91cf00b79bb6c7c6f99e44942b43a7141db565556a4298f6ebe3a86fcc
)
FetchContent_GetProperties(azuresdkforcpp)
if(NOT azuresdkforcpp_POPULATED)
    FetchContent_Populate(azuresdkforcpp)
    add_subdirectory(
        ${azuresdkforcpp_SOURCE_DIR}/sdk/core/azure-core
        ${azuresdkforcpp_BINARY_DIR}/sdk/core/azure-core
        EXCLUDE_FROM_ALL)
    add_subdirectory(
        ${azuresdkforcpp_SOURCE_DIR}/sdk/identity/azure-identity
        ${azuresdkforcpp_BINARY_DIR}/sdk/identity/azure-identity
        EXCLUDE_FROM_ALL)
    add_subdirectory(
        ${azuresdkforcpp_SOURCE_DIR}/sdk/storage/azure-storage-common
        ${azuresdkforcpp_BINARY_DIR}/sdk/storage/azure-storage-common
        EXCLUDE_FROM_ALL)
    add_subdirectory(
        ${azuresdkforcpp_SOURCE_DIR}/sdk/storage/azure-storage-blobs
        ${azuresdkforcpp_BINARY_DIR}/sdk/storage/azure-storage-blobs
        EXCLUDE_FROM_ALL)
endif()
