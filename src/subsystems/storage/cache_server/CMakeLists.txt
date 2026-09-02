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

project(cache_server LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

set(CACHE_SERVER_EXEC CacheServer)

execute_process(
    COMMAND /opt/protobufstatic-3.0.0/bin/protoc
                --proto_path=${DACS_SOURCE_ROOT}/src/subsystems/storage/cache_server/snapshot/proto
                --cpp_out=${DACS_SOURCE_ROOT}/src/subsystems/storage/cache_server/snapshot/proto
                ${DACS_SOURCE_ROOT}/src/subsystems/storage/cache_server/snapshot/proto/snapshot.proto)

file(GLOB CACHE_SERVER_UTILS_SOURCES ./utils/Utils.cpp)

add_library(CacheServerUtils SHARED ${CACHE_SERVER_UTILS_SOURCES})

target_include_directories(CacheServerUtils PRIVATE
    ${DACS_SOURCE_ROOT}/src/subsystems/storage/common
    ${DACS_SOURCE_ROOT}/src/subsystems/storage/common/proto/compiled
)

add_dependencies(CacheServerUtils
    StorageCommon
    CacheProto
)

file(GLOB CACHE_SERVER_SOURCES *cpp ./operations/*cpp ./utils/*cpp ./state/*cpp ./infiniband/*cpp
    ./snapshot/*cpp ./snapshot/proto/*cc ${DACS_SOURCE_ROOT}/src/subsystems/storage/common/*cpp)

add_executable(${CACHE_SERVER_EXEC} ${CACHE_SERVER_SOURCES})

target_include_directories(${CACHE_SERVER_EXEC} PRIVATE
    ${DACS_SOURCE_ROOT}/src/subsystems/common/native_lib/diagnostics
    ${DACS_SOURCE_ROOT}/src/subsystems/storage/common
    ${DACS_SOURCE_ROOT}/src/subsystems/storage/common/proto/compiled
    ${DACS_SOURCE_ROOT}/src/subsystems/storage/locator
    ${CMAKE_CURRENT_LIST_DIR}
    ./operations
    ./utils
    ./state
    ./infiniband
    ./snapshot
    ./snapshot/proto
    /opt/protobufstatic-3.0.0/include
)

# Statically link to precompiled protobuf library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)

SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)

add_dependencies(${CACHE_SERVER_EXEC}
    StorageCommon
    CacheProto
    DACSLocator
    diagnostics
)

target_link_libraries(CacheServer 
    StorageCommon
    CacheProto
    DACSLocator
    xxhash
    stdc++fs
    dl
    rt
    pthread
    ibverbs
    prometheus-cpp-pull
    prometheus-cpp-core
    crypto
    diagnostics
    staticprotobuf
)

add_custom_command(
    TARGET ${CACHE_SERVER_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CACHE_SERVER_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

add_executable(DACS::CacheServer ALIAS CacheServer)

if(DACS_BUILD_TESTS)
    add_subdirectory(./test/)
    enable_testing()

    file(GLOB CACHE_SERVER_TEST_SOURCES ${CACHE_SERVER_SOURCES} ./unittest/*cpp)
    list(FILTER CACHE_SERVER_TEST_SOURCES EXCLUDE REGEX ".*Server\\.cpp$")

    add_executable(CacheServerUnitTests ${CACHE_SERVER_TEST_SOURCES})

    target_include_directories(CacheServerUnitTests PRIVATE
        ${DACS_SOURCE_ROOT}/src/subsystems/common/native_lib/diagnostics
        ${DACS_SOURCE_ROOT}/src/subsystems/storage/common
        ${DACS_SOURCE_ROOT}/src/subsystems/storage/common/proto/compiled
        ${DACS_SOURCE_ROOT}/src/subsystems/storage/locator
        .
        ./operations
        ./utils
        ./state
        ./infiniband
        ./snapshot
        ./snapshot/proto
        /opt/protobufstatic-3.0.0/include)

    add_dependencies(CacheServerUnitTests
        StorageCommon
        CacheProto
        DACSLocator
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
    )

    target_link_libraries(CacheServerUnitTests
        StorageCommon
        CacheProto
        DACSLocator
        pthread
        ibverbs
        crypto
        diagnostics
        staticprotobuf
        stdc++fs
        prometheus-cpp-pull
        prometheus-cpp-core
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        -L${DEP_INSTALL_DIR}/lib
    )

    add_test(NAME CacheServerUnitTests COMMAND CacheServerUnitTests)
endif()
