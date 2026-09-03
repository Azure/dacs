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

# The fmt project for formatting and related vars. Embedded consumers may use
# a dependency image that does not preinstall DACS's fmt project.
if(EXISTS "${CMAKE_EXTERNAL_PROJECT}/fmt_include.cmake")
    include(${CMAKE_EXTERNAL_PROJECT}/fmt_include.cmake)
else()
    include(FetchContent)
    FetchContent_Declare(fmt
        URL "https://github.com/fmtlib/fmt/releases/download/7.1.0/fmt-7.1.0.zip"
        URL_HASH "SHA256=308af4e36ee3ab527b51014a2a5d862682c84f5d16f7a597aea34c84853cbcb0")
    FetchContent_MakeAvailable(fmt)
    include_directories(${fmt_SOURCE_DIR}/include)
endif()

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

# Enable strict diagnostics only after declaring third-party projects. Directory
# compile options are inherited by subdirectories, so placing this earlier also
# applies DACS's -Werror policy to external SDK source that we do not own.
# -Werror alone does not enable diagnostics; in particular, Clang accepts an
# implicit ssize_t-to-int conversion unless -Wshorten-64-to-32 is enabled.
add_compile_options(
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wall>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wpedantic>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wconversion>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wsign-conversion>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wshorten-64-to-32>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Werror>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wall>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wpedantic>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wconversion>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wsign-conversion>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wshorten-64-to-32>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror>
)

# ctest
include(CTest)

# Globaly available headers from dacs project
include_directories(/include)
include_directories(src/subsystems/common/native_lib/diagnostics/)
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
