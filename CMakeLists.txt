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

# Project name, build system and c++ standards used
cmake_minimum_required(VERSION 3.19)

# Load toolchain file before project() command
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake)
endif()

project(DACS LANGUAGES CXX C)

set(CMAKE_BINARY_DIR $ENV{CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $ENV{CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $ENV{CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_REMOTE_PUBLISH_DIRECTORY $ENV{CMAKE_REMOTE_PUBLISH_DIRECTORY})
set(CTEST_REMOTE_LIB_DIRECTORY $ENV{CTEST_REMOTE_LIB_DIRECTORY})

# Default standard in the project is C99 & C++23
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED $ENV{CXX_STANDARD_REQUIRED})
set(CMAKE_C_STANDARD $ENV{CMAKE_C_STANDARD})
set(C_STANDARD_REQUIRED $ENV{C_STANDARD_REQUIRED})

# Directory containing external project import files
set(CMAKE_DEP_INSTALL_DIR $ENV{CMAKE_DEP_INSTALL_DIR})
set(CMAKE_DEP_CMAKE_DIR ${CMAKE_DEP_INSTALL_DIR}/cmake)
set(CMAKE_EXTERNAL_PROJECT ${CMAKE_DEP_CMAKE_DIR}/external)
set(CMAKE_MODULE_PATH ${CMAKE_DEP_CMAKE_DIR}/external)

add_definitions(-D_GLIBCXX_USE_NANOSLEEP)

# Treat warnings as errors
add_compile_options(-Werror)


# Compile time symbol checking like check_cxx_symbol_exists(std::fopen "cstdio" HAVE_STD_FOPEN)
include(CheckCXXSymbolExists)

# Add DL libs
link_libraries(${CMAKE_DL_LIBS})

# Add boost headers
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# Enable referencing git projects dependencies
include(ExternalProject)

# Include all pre-built dependency projects
add_subdirectory(${CMAKE_DEP_INSTALL_DIR}/cmake ${CMAKE_DEP_INSTALL_DIR} EXCLUDE_FROM_ALL)

# The GTest git hub project for testing and testing related vars
include(${CMAKE_EXTERNAL_PROJECT}/gtest_include.cmake)

# The fmt project for formatting and related vars
include(${CMAKE_EXTERNAL_PROJECT}/fmt_include.cmake)

# serialization support through json. It supports json, bson, cbor etc.
include(${CMAKE_EXTERNAL_PROJECT}/json_include.cmake)

#Fast Hashing with xxHash - required for criu deduping
include(${CMAKE_EXTERNAL_PROJECT}/xxHash_include.cmake)

# OpenTelemetry metrics SDK and OTLP gRPC exporter
include(${CMAKE_EXTERNAL_PROJECT}/opentelemetry_cpp_include.cmake)

#prometheus support through https://github.com/jupp0r/prometheus-cpp/releases 
include(${CMAKE_EXTERNAL_PROJECT}/prometheus_cpp_include.cmake)

# Azure storage lite lib - to communicate with blob services
include(${CMAKE_EXTERNAL_PROJECT}/storagelite_include.cmake)

# Azure SDK for C++ - for direct blob access with Entra ID auth (provides azure-identity and azure-storage-blobs)
include(${CMAKE_SOURCE_DIR}/src/images/native-build/cmake/external/azure-sdk-cpp.cmake)

# Google protobuf static lib
include(${CMAKE_EXTERNAL_PROJECT}/protobufstatic_include.cmake)

# ctest
include(CTest)

# Globaly available headers from dacs project
include_directories(/include)
include_directories(src/subsystems/common/native_lib/diagnostics/)
include_directories(src/subsystems/common/native_lib/diagnostics/)

# Enable testing at the root level
enable_testing()

add_subdirectory(src/subsystems/common/native_lib/diagnostics)
add_subdirectory(src/subsystems/common/native_lib/diagnostics.tests)
add_subdirectory(src/subsystems/storage/common/)
add_subdirectory(src/subsystems/storage/common/proto/)
add_subdirectory(src/subsystems/storage/common/libc_wrapper/)
add_subdirectory(src/subsystems/storage/cache_client/connection/)
add_subdirectory(src/subsystems/storage/common/ibverbs_dlsym/)
add_subdirectory(src/subsystems/storage/cache_client/encryption_utils/)
add_subdirectory(src/subsystems/storage/storage_intercept/)
add_subdirectory(src/subsystems/storage/cache_server/)
add_subdirectory(src/subsystems/storage/cache_client/)
add_subdirectory(src/subsystems/storage/cache_client/liveness_probe/)
add_subdirectory(src/subsystems/storage/utils/)
add_subdirectory(src/subsystems/storage/stress_tests/)
add_subdirectory(src/subsystems/storage/tools/)
