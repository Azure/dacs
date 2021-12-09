project(cache_server LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

include_directories(${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
                    ${CMAKE_SOURCE_DIR}/src/subsystems/reliability/common/native
                    .
                    ./operations
                    ./proto
                    ./utils
                    )

set(CACHE_SERVER_EXEC CacheServer)

# Generate protobuf source files
execute_process(
    COMMAND protoc
                --proto_path=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto 
                --cpp_out=${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto
                ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/proto/cache.proto)

file(GLOB CACHE_SERVER_SOURCES *cpp ./operations/*cpp ./utils/*cpp ./proto/*cc)

add_executable(${CACHE_SERVER_EXEC} ${CACHE_SERVER_SOURCES})

target_link_libraries(
    ${CACHE_SERVER_EXEC}
    pthread
    protobuf
    diagnostics
    reliability_common
)

add_custom_command(
    TARGET ${CACHE_SERVER_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${CACHE_SERVER_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
