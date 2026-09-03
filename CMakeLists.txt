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

if(DEFINED ENV{CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        "$ENV{CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
endif()
if(DEFINED ENV{CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "$ENV{CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()
if(DEFINED ENV{CMAKE_REMOTE_PUBLISH_DIRECTORY})
    set(CMAKE_REMOTE_PUBLISH_DIRECTORY
        "$ENV{CMAKE_REMOTE_PUBLISH_DIRECTORY}")
else()
    set(CMAKE_REMOTE_PUBLISH_DIRECTORY
        "${PROJECT_BINARY_DIR}/publish")
endif()

# Default standard in the project is C99 & C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(DACS_BUILD_CACHE_CLIENT "Build the DACS cache client" ON)
option(DACS_BUILD_CACHE_SERVER "Build the DACS cache server" ON)
option(DACS_BUILD_STORAGE_INTERCEPT "Build DACS storage interception libraries" ON)
option(DACS_BUILD_LIVENESS_PROBE "Build the DACS cache client liveness probe" ON)
option(DACS_BUILD_TESTS "Build DACS storage tests" ON)
option(DACS_BUILD_INTEGRATION_TESTS
    "Build tests that require live services or build tools" OFF)
option(DACS_BUILD_TOOLS "Build DACS storage tools" ON)
option(DACS_INSTALL_SDK "Install the DACS client SDK and runtime artifacts" OFF)
option(DACS_BUILD_CACHE_CLIENT_STATIC_ARCHIVE
    "Merge the cache client and static protobuf into one archive" OFF)

add_definitions(-DBUILD_GIT_COMMIT_HASH="${GIT_COMMIT_HASH}" -D_GLIBCXX_USE_NANOSLEEP)

# Treat warnings as errors
add_compile_options(-Werror)


# Compile time symbol checking like check_cxx_symbol_exists(std::fopen "cstdio" HAVE_STD_FOPEN)
include(CheckCXXSymbolExists)

# Add DL libs
link_libraries(${CMAKE_DL_LIBS})

include(cmake/Dependencies.cmake)

# ctest
include(CTest)

# Globaly available headers from dacs project
include_directories(src/subsystems/common/native_lib/diagnostics/)

# Enable testing at the root level
enable_testing()

add_subdirectory(src/subsystems/storage/)
if(DACS_BUILD_TESTS)
    add_subdirectory(src/subsystems/common/native_lib/diagnostics.tests)
endif()
if(DACS_BUILD_TOOLS)
    add_subdirectory(src/subsystems/storage/utils/)
    add_subdirectory(src/subsystems/storage/stress_tests/)
    add_subdirectory(src/subsystems/storage/tools/)
endif()
