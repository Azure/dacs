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

project(storage_intercept LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")
add_definitions(-D_GLIBCXX_USE_NANOSLEEP)

include_directories(
        ${DACS_SOURCE_ROOT}/src/subsystems/common/native_lib/diagnostics
        ${DACS_SOURCE_ROOT}/src/subsystems/storage/common
        ${DEP_INSTALL_DIR}/include
        ./common
        ./remote_client
        ./handlers
        ./remote_client/cache
)

file(GLOB STORAGE_INTERCEPT_SOURCES_COMMON ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./common/*.cpp ./handlers/*cpp ./interceptors/StorageInterceptorImpl.cpp
        ./remote_client/cache/*)

set_source_files_properties(remote_client/StreamingClient.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")

file(GLOB STORAGE_INTERCEPT_SOURCES_INTERCEPTOR ${STORAGE_INTERCEPT_SOURCES_COMMON} ./interceptors/StorageInterceptor.cpp)
file(GLOB STORAGE_INTERCEPT_SOURCES_CACHE_CLIENT ${STORAGE_INTERCEPT_SOURCES_COMMON} ./interceptors/CacheClientInterface.cpp)
add_library(CacheClient SHARED ${STORAGE_INTERCEPT_SOURCES_CACHE_CLIENT})
add_library(DACS::StorageRemoteClient ALIAS CacheClient)

set(CommonDeps CacheClientLib StorageCommon CacheProto ClientConnections EncryptionUtils diagnostics
    curl azure-storage-lite jsoncpp dl uuid crypto protobuf::libprotobuf Azure::azure-identity
    Azure::azure-storage-blobs)

# Linker flags to hide static symbols
set(HIDE_STATIC_SYMBOLS_FLAGS "-Wl,--exclude-libs,ALL")

# Core storage dependencies
set(STORAGE_CORE_DEPS StorageCommon CacheProto ClientConnections EncryptionUtils)

# storage intercept library meant to be LD_PRELOADed
add_library(StorageIntercept SHARED ${STORAGE_INTERCEPT_SOURCES_INTERCEPTOR})
add_dependencies(StorageIntercept StorageLibcDlsymWrapper ${STORAGE_CORE_DEPS})
target_link_libraries(StorageIntercept StorageLibcDlsymWrapper ${CommonDeps})
target_link_options(StorageIntercept PRIVATE ${HIDE_STATIC_SYMBOLS_FLAGS})

# cache storage client library called directly without LD_PRELOAD (from python package for example)
# No dlsym wrapper needed here.
add_library(StorageDirect SHARED ${STORAGE_INTERCEPT_SOURCES_INTERCEPTOR}
        ./interceptors/BlobSiWrapper.cpp)
add_dependencies(StorageDirect StorageLibcDirectWrapper ${STORAGE_CORE_DEPS})
target_link_libraries(StorageDirect StorageLibcDirectWrapper ${CommonDeps})
target_link_options(StorageDirect PRIVATE ${HIDE_STATIC_SYMBOLS_FLAGS})

add_dependencies(CacheClient StorageLibcDirectWrapper ${STORAGE_CORE_DEPS})
target_link_libraries(CacheClient PRIVATE StorageLibcDirectWrapper ${CommonDeps})
target_link_options(CacheClient PRIVATE ${HIDE_STATIC_SYMBOLS_FLAGS})
target_include_directories(CacheClient
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/remote_client>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/common>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dacs/storage_remote>)
set_target_properties(CacheClient PROPERTIES
    EXPORT_NAME StorageRemoteClient)

add_custom_command(
    TARGET CacheClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)

add_custom_command(
    TARGET CacheClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${DACS_SOURCE_ROOT}/src/subsystems/storage/storage_intercept/interceptors/CacheClientInterface.h
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)

if(DACS_INSTALL_SDK)
    install(TARGETS CacheClient
        EXPORT DACSTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/storage_remote)
    install(FILES
        remote_client/IRemoteClient.h
        remote_client/RemoteClientProvider.h
        common/DirectoryEntry.h
        common/FileAttributes.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/storage_remote)
endif()

if(DACS_BUILD_TOOLS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FUSE3 REQUIRED fuse3 IMPORTED_TARGET)

    file(GLOB DACSFS_SOURCES ${STORAGE_INTERCEPT_SOURCES_COMMON} fuse/fuse.cpp)

    add_executable(dacsfs ${DACSFS_SOURCES})
    target_compile_definitions(dacsfs PRIVATE FUSE_USE_VERSION=31 STORAGE_INTERCEPT_FUSE=1)
    target_link_libraries(dacsfs PRIVATE
        PkgConfig::FUSE3 StorageLibcDirectWrapper ${CommonDeps} ibverbs)

    add_custom_command(
        TARGET dacsfs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dacsfs
        ${CMAKE_REMOTE_PUBLISH_DIRECTORY})
endif()

if(DACS_BUILD_TESTS)
    enable_testing()

    file(GLOB STORAGE_INTERCEPT_SOURCES_TEST
        ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./handlers/*cpp ./common/*.cpp ./remote_client/cache/* ./tests/*cpp
        ./interceptors/BlobSiWrapper.cpp
        ./interceptors/CacheClientInterface.cpp
        ./interceptors/StorageInterceptorImpl.cpp)

    add_executable(StorageInterceptUnitTests ${STORAGE_INTERCEPT_SOURCES_TEST})

# Ensure debugging symbols are enabled
    set_target_properties(StorageInterceptUnitTests PROPERTIES COMPILE_FLAGS "-g -O0")

    target_compile_definitions(StorageInterceptUnitTests PRIVATE STORAGE_INTERCEPT_UT=1)

    target_include_directories(StorageInterceptUnitTests PRIVATE
        ./adls
        ./remote_client
        ./remote_client/adls
        ./remote_client/blob
        ./remote_client/mock
        ./remote_client/cache
        ./handlers
        ./interceptors)

    add_dependencies(StorageInterceptUnitTests
        StorageCommon
        StorageLibcDlsymWrapper
        CacheProto
        ClientConnections
        EncryptionUtils
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        )

    target_link_libraries(StorageInterceptUnitTests
        StorageCommon
        StorageLibcDlsymWrapper
        EncryptionUtils
        ${CommonDeps}
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        backtrace)

    add_test(NAME StorageInterceptUnitTests
        COMMAND StorageInterceptUnitTests)

    if(DACS_BUILD_INTEGRATION_TESTS)
        add_subdirectory(./integration_tests/)
    endif()
endif()
