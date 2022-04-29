project(cache_server LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

include_directories(${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
                    ${CMAKE_SOURCE_DIR}/src/subsystems/reliability/common/native
                    .
                    ./operations
                    ./proto
                    ./utils
                    ./state
                    /opt/protobufstatic-3.0.0/include
                    )

set(CACHE_SERVER_EXEC CacheServer)

# Generate protobuf source files
execute_process(
    COMMAND /opt/protobufstatic-3.0.0/bin/protoc
                --proto_path=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto 
                --cpp_out=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto
                ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto/cache.proto)

file(GLOB CACHE_SERVER_SOURCES *cpp ./operations/*cpp ./utils/*cpp ./state/*cpp ./proto/*cc)

add_executable(${CACHE_SERVER_EXEC} ${CACHE_SERVER_SOURCES})

# Statically link to precompiled protobuf library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)
SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)

target_link_libraries(
    ${CACHE_SERVER_EXEC}
    pthread
    crypto
    diagnostics
    reliability_common
    staticprotobuf
)

add_custom_command(
    TARGET ${CACHE_SERVER_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CACHE_SERVER_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
