project(stress_tests LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

# Statically link to precompiled protobuf library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)

SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)

add_executable(CacheStorageClient 
    StorageClient.cpp)

add_dependencies(CacheStorageClient storage_utils)

target_link_libraries(CacheStorageClient storage_utils pthread stdc++fs)

add_custom_command(
    TARGET CacheStorageClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

add_custom_command(
    TARGET CacheStorageClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/stress_tests/launcher.py
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

add_custom_command(
    TARGET CacheStorageClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/stress_tests/StorageClient.py
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

add_custom_command(
    TARGET CacheStorageClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/stress_tests/storageIntercept.config
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

add_custom_command(
    TARGET CacheStorageClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/CacheStorageClient
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

add_executable(DeleteClient
    DeleteClient.cpp)

# Need the protobuf files to be created before we can build this executable
add_dependencies(DeleteClient StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto CacheServer)

target_include_directories(DeleteClient PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/operations
)

target_link_libraries(DeleteClient StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto pthread staticprotobuf)

add_custom_command(
    TARGET DeleteClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DeleteClient
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)


# ---------------------------------------------------------------------------------------------------------------------#
# Perf Client
# ---------------------------------------------------------------------------------------------------------------------#
find_package(Boost COMPONENTS program_options system REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(PerfClient
    perf_tests/PerfClient.cpp)

add_dependencies(PerfClient CacheClientLib ClientConnections storage_utils)

target_link_libraries(PerfClient PRIVATE CacheClientLib ClientConnections storage_utils pthread diagnostics stdc++fs Boost::program_options Boost::system)

target_include_directories(PerfClient PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common/
)

add_custom_command(
    TARGET PerfClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/PerfClient
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cache-test)

# ---------------------------------------------------------------------------------------------------------------------#
