
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
)

add_library(CacheClientLib STATIC
    CacheClient.cpp
    CacheClientImpl.cpp
)

set(CommonDeps StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto EncryptionUtils)
set(CommonLinks StorageLibcDlsymWrapper
    ClientConnections
    CacheProto
    EncryptionUtils
    diagnostics
    pthread
    ibverbs
    xxhash)

add_dependencies(CacheClientSharedLib ${CommonDeps})
add_dependencies(CacheClientLib ${CommonDeps})

target_include_directories(CacheClientSharedLib PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(CacheClientLib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(CacheClientSharedLib ${CommonLinks})
target_link_libraries(CacheClientLib ${CommonLinks})

enable_testing()
add_executable(CacheClientLibTests tests/TestMain.cpp tests/CacheClientTests.cpp)
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

add_test(NAME CacheClientLibTests
    COMMAND CacheClientLibTests)

add_custom_command(
    TARGET CacheClientLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_client/CacheClient.h
    ${CMAKE_REMOTE_PUBLISH_DIRECTORY}/)
