project(stress_tests LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

add_executable(CacheStorageClient 
    StorageClient.cpp
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/integration_tests/src/ThreadPool.cpp)

target_include_directories(CacheStorageClient PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/integration_tests/src/)

target_link_libraries(CacheStorageClient pthread stdc++fs)

add_executable(DeleteClient
    DeleteClient.cpp)

# Need the protobuf files to be created before we can build this executable
add_dependencies(DeleteClient CacheProto CacheServer)

target_include_directories(DeleteClient PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/operations/
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/utils/)

target_link_libraries(DeleteClient CacheProto pthread protobuf)