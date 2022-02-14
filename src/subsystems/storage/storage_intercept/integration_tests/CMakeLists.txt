project(storage_intercept_integ_test LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

# Integration test for storage interception
add_executable(StorageInterceptIntegTest
    src/ThreadPool.cpp
    src/TestClient.cpp
)

target_link_libraries(StorageInterceptIntegTest pthread)