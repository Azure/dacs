project(cache_server LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

include_directories(${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
                    ${CMAKE_SOURCE_DIR}/src/subsystems/reliability/common/native
                    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common
                    .
                    ./operations
                    ./utils
                    ./state
                    ./infiniband
                    ./snapshot
                    ./snapshot/proto
                    /opt/protobufstatic-3.0.0/include
                    )

set(CACHE_SERVER_EXEC CacheServer)

execute_process(
    COMMAND /opt/protobufstatic-3.0.0/bin/protoc
                --proto_path=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/snapshot/proto 
                --cpp_out=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/snapshot/proto
                ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/snapshot/proto/snapshot.proto)

file(GLOB CACHE_SERVER_SOURCES *cpp ./operations/*cpp ./utils/*cpp ./state/*cpp ./infiniband/*cpp
    ./snapshot/*cpp ./snapshot/proto/*cc ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common/*cpp)

add_executable(${CACHE_SERVER_EXEC} ${CACHE_SERVER_SOURCES})

# Statically link to precompiled protobuf library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)

SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)

target_link_libraries(
    ${CACHE_SERVER_EXEC}
    CacheProto
    pthread
    ibverbs
    prometheus-cpp-pull
    prometheus-cpp-core
    crypto
    diagnostics
    reliability_common
    staticprotobuf
)

add_custom_command(
    TARGET ${CACHE_SERVER_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CACHE_SERVER_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

# Cache Server test program
add_subdirectory(./test/)

# Cache Server unit tests
enable_testing()

file(GLOB CACHE_SERVER_TEST_SOURCES ${CACHE_SERVER_SOURCES} ./unittest/*cpp)
list(FILTER CACHE_SERVER_TEST_SOURCES EXCLUDE REGEX ".*Server\\.cpp$")

add_executable(CacheServerUnitTests ${CACHE_SERVER_TEST_SOURCES})

target_include_directories(CacheServerUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
    ${CMAKE_SOURCE_DIR}/src/subsystems/reliability/common/native
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common
    .
    ./operations
    ./utils
    ./state
    ./infiniband
    ./snapshot
    ./snapshot/proto
    /opt/protobufstatic-3.0.0/include)

add_dependencies(CacheServerUnitTests
        CacheProto
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        )

target_link_libraries(CacheServerUnitTests
        CacheProto
        pthread
        ibverbs
        crypto
        diagnostics
        reliability_common
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
