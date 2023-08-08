project(stress_tests LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

add_executable(CacheStorageClient 
    StorageClient.cpp)

add_dependencies(CacheStorageClient storage_utils)

target_link_libraries(CacheStorageClient storage_utils pthread stdc++fs)

add_executable(DeleteClient
    DeleteClient.cpp)

# Need the protobuf files to be created before we can build this executable
add_dependencies(DeleteClient StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto CacheServer)

target_include_directories(DeleteClient PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/operations
)

target_link_libraries(DeleteClient StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto pthread protobuf)