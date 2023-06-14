project(test_client LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

include_directories(.
                    ../operations
                    ../proto
                    ../utils
                    ../state
                    ../infiniband
                    /opt/protobufstatic-3.0.0/include
                    )

set(TEST_CLIENT_EXEC cacheServerTestClient)

add_executable(${TEST_CLIENT_EXEC} TestClient.cpp)

target_link_libraries(
    ${TEST_CLIENT_EXEC}
    pthread
    ibverbs
    staticprotobuf
    xxhash
)

add_custom_command(
    TARGET ${TEST_CLIENT_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TEST_CLIENT_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})