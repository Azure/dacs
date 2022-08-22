project(storage_intercept LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

# Generate protobuf source files
execute_process(
    COMMAND /opt/protobufstatic-3.0.0/bin/protoc
                --proto_path=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto 
                --cpp_out=${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/remote_client/cache
                ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto/cache.proto)

include_directories(
        ${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
        ${DEP_INSTALL_DIR}/include
        ./common
        ./remote_client
        ./handlers
        ./remote_client/cache
	/opt/protobufstatic-3.0.0/include
)

file(GLOB STORAGE_INTERCEPT_SOURCES ./common/*cpp ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./handlers/*cpp ./interceptors/*cpp
        ./remote_client/cache/*)

# StorageIntercept Lib
add_library(StorageIntercept SHARED ${STORAGE_INTERCEPT_SOURCES})

# Don't expose internally linked interfaces via the StorageIntercept shared library
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--exclude-libs,ALL")

# Statically link to precompiled protobuf library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)
SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)
target_link_libraries(StorageIntercept diagnostics azure-storage-lite curl jsoncpp dl staticprotobuf)

# Also copy the blob library to output folder
add_custom_command(
        TARGET StorageIntercept POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_DEP_INSTALL_DIR}/azurestoragelite-prefix/src/azurestoragelite-build/libazure-storage-lite.so
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

#StorageIntercept Unit Tests
enable_testing()

file(GLOB STORAGE_INTERCEPT_SOURCES_TEST ./common/*cpp ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./handlers/*cpp ./remote_client/cache/* ./tests/*cpp)

add_executable(StorageInterceptUnitTests ${STORAGE_INTERCEPT_SOURCES_TEST})

target_include_directories(StorageInterceptUnitTests PRIVATE
        ./adls
        ./common
        ./remote_client
        ./remote_client/adls
        ./remote_client/blob
        ./remote_client/mock
        ./remote_client/cache
        ./handlers)

add_dependencies(StorageInterceptUnitTests
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        )

target_link_libraries(StorageInterceptUnitTests
        diagnostics curl azure-storage-lite jsoncpp dl uuid crypto staticprotobuf
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        -L${DEP_INSTALL_DIR}/lib
        )

add_test(NAME StorageInterceptUnitTests
        COMMAND StorageInterceptUnitTests)

# Storage Intercept integration test
add_subdirectory(./integration_tests/)
