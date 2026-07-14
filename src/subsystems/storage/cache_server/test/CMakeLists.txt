project(test_client LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(TEST_CLIENT_EXEC cacheServerTestClient)

add_executable(${TEST_CLIENT_EXEC} TestClient.cpp)

add_dependencies(${TEST_CLIENT_EXEC} StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto)

target_include_directories(${TEST_CLIENT_EXEC} PRIVATE
                    .
                    ../operations
                    ../state
                    ../infiniband
                    /opt/protobufstatic-3.0.0/include
                    )

target_link_libraries(
    ${TEST_CLIENT_EXEC}
    StorageCommon
    StorageLibcDlsymWrapper
    ClientConnections
    CacheProto
    pthread
    ibverbs
    xxhash
)

add_custom_command(
    TARGET ${TEST_CLIENT_EXEC} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TEST_CLIENT_EXEC}> ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})