project(CacheClientSharedLib LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage-cache-client")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage-cache-client")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage-cache-client")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

# Two separate projects, shared lib for outside consumers, regular lib for use within project to reduce shared lib dependencies.
add_library(CacheClientSharedLib SHARED
    CacheClient.cpp
    CacheClientImpl.cpp
    utils/CacheClientRetry.cpp
)

add_library(CacheClientLib STATIC
    CacheClient.cpp
    CacheClientImpl.cpp
    utils/CacheClientRetry.cpp
)

set(CommonDeps StorageCommon StorageLibcDlsymWrapper IbverbsDlsym ClientConnections CacheProto EncryptionUtils)
set(CommonLinks StorageLibcDlsymWrapper
    IbverbsDlsym
    ClientConnections
    CacheProto
    EncryptionUtils
    diagnostics
    pthread
    xxhash)

add_dependencies(CacheClientSharedLib ${CommonDeps})
add_dependencies(CacheClientLib ${CommonDeps})

target_include_directories(CacheClientSharedLib PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(CacheClientLib PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(CacheClientSharedLib ${CommonLinks})
target_link_libraries(CacheClientLib ${CommonLinks})

enable_testing()
add_executable(CacheClientLibTests tests/TestMain.cpp tests/CacheClientTests.cpp tests/ConsistentHashingTests.cpp tests/CacheClientRetryUtilTests.cpp tests/ConnectionManagerTests.cpp)
add_dependencies(CacheClientLibTests CacheClientLib)
target_link_libraries(CacheClientLibTests
    CacheClientLib
    pthread
    ibverbs
    xxhash
    diagnostics
    ${GMOCK_LIBRARY}
    ${GMOCK_MAIN_LIBRARY}
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
)

target_include_directories(CacheClientLibTests PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/connection
    tests  
)

# Separate test binary for IBConnection with fake ibverbs stubs.
# Compiles IBConnection.cpp directly and provides its own ibverbs symbols,
# avoiding the DlsymWrapper so tests run without real IB hardware.
add_executable(IBConnectionTests
    tests/IBConnectionTests.cpp
    tests/TestMain.cpp
    connection/IBConnection.cpp
)
add_dependencies(IBConnectionTests StorageCommon StorageLibcDlsymWrapper diagnostics)
target_link_libraries(IBConnectionTests
    diagnostics
    pthread
    dl
    xxhash
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
)
target_include_directories(IBConnectionTests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/connection
    $<TARGET_PROPERTY:StorageCommon,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:StorageLibcDlsymWrapper,INTERFACE_INCLUDE_DIRECTORIES>
    tests
)

# CMake archives dont include dependencies, only references.
# So, merge all dependent objects into one archive.
add_library(CacheClientStaticLibWithoutProtobuf STATIC
    $<TARGET_OBJECTS:CacheClientLib>
    $<TARGET_OBJECTS:StorageCommon>
    $<TARGET_OBJECTS:StorageLibcDlsymWrapper>
    $<TARGET_OBJECTS:IbverbsDlsym>
    $<TARGET_OBJECTS:ClientConnections>
    $<TARGET_OBJECTS:CacheProto>
    $<TARGET_OBJECTS:EncryptionUtils>
    $<TARGET_OBJECTS:diagnostics>
)

# Merge protobuf into the final archive.
add_custom_command(
    TARGET CacheClientStaticLibWithoutProtobuf 
    POST_BUILD
    COMMAND 
        ${CMAKE_CURRENT_SOURCE_DIR}/merge_archives.sh 
        ${CMAKE_REMOTE_PUBLISH_DIRECTORY}/libCacheClientStaticLib.a 
        $<TARGET_FILE:CacheClientStaticLibWithoutProtobuf> 
        /opt/protobufstatic-3.0.0/lib/libprotobuf.a
)

add_test(NAME CacheClientLibTests
    COMMAND CacheClientLibTests)

add_custom_command(
    TARGET CacheClientLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/cache_client/CacheClient.h
    ${CMAKE_REMOTE_PUBLISH_DIRECTORY}/)