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

project(CacheClientSharedLib LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage-cache-client")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage-cache-client")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage-cache-client")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

# Two separate projects, shared lib for outside consumers, regular lib for use within project to reduce shared lib dependencies.
add_library(CacheClientSharedLib SHARED
    CacheClient.cpp
    CacheClientImpl.cpp
    utils/CacheClientRetry.cpp
    $<TARGET_OBJECTS:StorageLibcDirectWrapper>
)

add_library(CacheClientLib STATIC
    CacheClient.cpp
    CacheClientImpl.cpp
    utils/CacheClientRetry.cpp
)

set(CommonDeps StorageCommon StorageMetrics StorageLibcDlsymWrapper IbverbsDlsym ClientConnections CacheProto EncryptionUtils)
set(CommonLinks StorageLibcDlsymWrapper
    IbverbsDlsym
    ClientConnections
    CacheProto
    EncryptionUtils
    StorageMetrics
    diagnostics
    pthread
    xxhash)

add_dependencies(CacheClientSharedLib ${CommonDeps} StorageLibcDirectWrapper)
add_dependencies(CacheClientLib ${CommonDeps})

target_include_directories(CacheClientSharedLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dacs/cache_client>)
target_include_directories(CacheClientLib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(CacheClientSharedLib PRIVATE ${CommonLinks})
target_link_libraries(CacheClientLib PUBLIC ${CommonLinks})

set_target_properties(CacheClientSharedLib PROPERTIES
    EXPORT_NAME CacheClientShared)

add_library(DACS::CacheClient ALIAS CacheClientLib)
add_library(DACS::CacheClientShared ALIAS CacheClientSharedLib)

if(DACS_BUILD_TESTS)
    enable_testing()
    add_executable(CacheClientLibTests tests/TestMain.cpp tests/CacheClientTests.cpp tests/ConsistentHashingTests.cpp tests/CacheClientRetryUtilTests.cpp tests/ConnectionManagerTests.cpp)
    add_dependencies(CacheClientLibTests CacheClientLib)
    target_link_libraries(CacheClientLibTests
        CacheClientLib
        pthread
        ibverbs
        xxhash
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
    )

    target_include_directories(CacheClientLibTests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/connection
        tests
    )

    add_executable(IBConnectionTests
        tests/IBConnectionTests.cpp
        tests/TestMain.cpp
        connection/IBConnection.cpp
    )
    add_dependencies(IBConnectionTests StorageCommon StorageLibcDlsymWrapper diagnostics)
    target_link_libraries(IBConnectionTests
        diagnostics
        pthread
        dl
        xxhash
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
    )
    target_include_directories(IBConnectionTests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/connection
        $<TARGET_PROPERTY:StorageCommon,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:StorageLibcDlsymWrapper,INTERFACE_INCLUDE_DIRECTORIES>
        tests
    )

    add_test(NAME CacheClientLibTests COMMAND CacheClientLibTests)
endif()

# CMake archives dont include dependencies, only references.
# So, merge all dependent objects into one archive.
add_library(CacheClientStaticLibWithoutProtobuf STATIC
    $<TARGET_OBJECTS:CacheClientLib>
    $<TARGET_OBJECTS:StorageCommon>
    $<TARGET_OBJECTS:StorageLibcDlsymWrapper>
    $<TARGET_OBJECTS:IbverbsDlsym>
    $<TARGET_OBJECTS:ClientConnections>
    $<TARGET_OBJECTS:CacheProto>
    $<TARGET_OBJECTS:EncryptionUtils>
    $<TARGET_OBJECTS:StorageMetrics>
    $<TARGET_OBJECTS:diagnostics>
)

if(DACS_BUILD_CACHE_CLIENT_STATIC_ARCHIVE)
    get_target_property(_protobuf_library_type
        protobuf::libprotobuf TYPE)
    if(NOT _protobuf_library_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "DACS_BUILD_CACHE_CLIENT_STATIC_ARCHIVE requires a static "
            "protobuf::libprotobuf target")
    endif()

    set(CACHE_CLIENT_STATIC_ARCHIVE
        "${CMAKE_CURRENT_BINARY_DIR}/libCacheClientStaticLib.a")
    add_custom_command(
        TARGET CacheClientStaticLibWithoutProtobuf
        POST_BUILD
        BYPRODUCTS ${CACHE_CLIENT_STATIC_ARCHIVE}
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/merge_archives.sh
            ${CACHE_CLIENT_STATIC_ARCHIVE}
            $<TARGET_FILE:CacheClientStaticLibWithoutProtobuf>
            $<TARGET_FILE:protobuf::libprotobuf>
        COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_REMOTE_PUBLISH_DIRECTORY}
        COMMAND
            ${CMAKE_COMMAND} -E copy
            ${CACHE_CLIENT_STATIC_ARCHIVE}
            ${CMAKE_REMOTE_PUBLISH_DIRECTORY}/libCacheClientStaticLib.a)
endif()

add_custom_command(
    TARGET CacheClientLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_REMOTE_PUBLISH_DIRECTORY}
    COMMAND ${CMAKE_COMMAND} -E copy ${DACS_SOURCE_ROOT}/src/subsystems/storage/cache_client/CacheClient.h
        ${CMAKE_REMOTE_PUBLISH_DIRECTORY}/)

if(DACS_INSTALL_SDK)
    install(TARGETS CacheClientSharedLib
        EXPORT DACSTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/cache_client)
    if(DACS_BUILD_CACHE_CLIENT_STATIC_ARCHIVE)
        install(FILES
            ${CACHE_CLIENT_STATIC_ARCHIVE}
            DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
    install(FILES CacheClient.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/cache_client)
endif()