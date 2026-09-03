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

include(FetchContent)

find_package(Boost REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(Threads REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

function(dacs_import_library target)
    set(options)
    set(one_value_args HEADER LIBRARY)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "" ${ARGN})

    find_path(${target}_INCLUDE_DIR NAMES "${ARG_HEADER}" REQUIRED)
    find_library(${target}_LIBRARY NAMES "${ARG_LIBRARY}" REQUIRED)

    add_library(${target} UNKNOWN IMPORTED)
    set_target_properties(${target} PROPERTIES
        IMPORTED_LOCATION "${${target}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${${target}_INCLUDE_DIR}")
endfunction()

add_library(curl INTERFACE)
target_link_libraries(curl INTERFACE CURL::libcurl)

add_library(crypto INTERFACE)
target_link_libraries(crypto INTERFACE OpenSSL::Crypto)

dacs_import_library(jsoncpp
    HEADER jsoncpp/json/json.h
    LIBRARY jsoncpp)
dacs_import_library(uuid
    HEADER uuid/uuid.h
    LIBRARY uuid)
dacs_import_library(xxhash
    HEADER xxhash.h
    LIBRARY xxhash)
dacs_import_library(ibverbs
    HEADER infiniband/verbs.h
    LIBRARY ibverbs)

if(NOT TARGET fmt::fmt)
    FetchContent_Declare(
        fmt
        URL https://github.com/fmtlib/fmt/releases/download/7.1.0/fmt-7.1.0.zip
        URL_HASH SHA256=308af4e36ee3ab527b51014a2a5d862682c84f5d16f7a597aea34c84853cbcb0)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fmt)
endif()
get_target_property(DACS_FMT_INCLUDE_DIRS
    fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${DACS_FMT_INCLUDE_DIRS})

if(DACS_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
    FetchContent_MakeAvailable(googletest)
    set(GTEST_LIBRARY GTest::gtest)
    set(GTEST_MAIN_LIBRARY GTest::gtest_main)
    set(GMOCK_LIBRARY GTest::gmock)
    set(GMOCK_MAIN_LIBRARY GTest::gmock_main)
endif()

if(DACS_BUILD_STORAGE_INTERCEPT)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        azurestoragelite
        URL https://github.com/Azure/azure-storage-cpplite/archive/33a807a3984681a604c41659a4a2030ea81194e7.tar.gz
        URL_HASH SHA256=c781b7e6a3d7706657b024191cbaa35b7a06fce9b3d0287836eee321a9ada18f)
    FetchContent_GetProperties(azurestoragelite)
    if(NOT azurestoragelite_POPULATED)
        FetchContent_Populate(azurestoragelite)
        add_subdirectory(
            "${azurestoragelite_SOURCE_DIR}"
            "${azurestoragelite_BINARY_DIR}"
            EXCLUDE_FROM_ALL)
    endif()
    target_compile_options(azure-storage-lite PRIVATE
        -Wno-error=deprecated-declarations)

    include(
        "${CMAKE_CURRENT_LIST_DIR}/../src/images/native-build/cmake/external/azure-sdk-cpp.cmake")
endif()

if(DACS_BUILD_CACHE_SERVER)
    set(ENABLE_PUSH OFF CACHE BOOL "" FORCE)
    set(ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        prometheus_cpp
        URL https://github.com/jupp0r/prometheus-cpp/releases/download/v1.3.0/prometheus-cpp-with-submodules.tar.gz
        URL_HASH SHA256=62bc2cc9772db2314dbaae506ae2a75c8ee897dab053d8729e86a637b018fdb6)
    FetchContent_MakeAvailable(prometheus_cpp)
endif()
