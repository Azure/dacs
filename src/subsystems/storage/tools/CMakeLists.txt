project(storage_tools LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

# Find required packages
find_package(OpenSSL REQUIRED)

# Create a static library for shared functionality
add_library(GetCacheKeyAndServerIndexLib STATIC
    ToolUtils.cpp)

# Set dependencies and includes for the library
add_dependencies(GetCacheKeyAndServerIndexLib ClientConnections StorageCommon CacheServerUtils diagnostics)

target_include_directories(GetCacheKeyAndServerIndexLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_client/connection
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_server/utils
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/common
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common
    ${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
)

target_link_libraries(GetCacheKeyAndServerIndexLib PUBLIC
    ClientConnections
    StorageCommon
    CacheServerUtils
    diagnostics
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    stdc++fs
)

set_property(TARGET GetCacheKeyAndServerIndexLib PROPERTY CXX_STANDARD 17)

# Create the main executable (just contains main function)
add_executable(GetCacheChunkProperties
    GetCacheChunkProperties.cpp)

target_link_libraries(GetCacheChunkProperties PRIVATE 
    GetCacheKeyAndServerIndexLib
)

set_property(TARGET GetCacheChunkProperties PROPERTY CXX_STANDARD 17)

enable_testing()
add_executable(GetCacheChunkPropertiesTest GetCacheChunkPropertiesTest.cpp)

# Link the test with the library to access the actual implementation
target_link_libraries(GetCacheChunkPropertiesTest 
    GetCacheKeyAndServerIndexLib
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
)
    
set_property(TARGET GetCacheChunkPropertiesTest PROPERTY CXX_STANDARD 17)

add_test(NAME GetCacheChunkPropertiesTest
    COMMAND GetCacheChunkPropertiesTest)

# Copy executables to test directory for convenience
add_custom_command(
    TARGET GetCacheChunkProperties POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/tools)

add_custom_command(
    TARGET GetCacheChunkProperties POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/GetCacheChunkProperties
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/tools)
