project(storage_intercept LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

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
        ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common
        ${DEP_INSTALL_DIR}/include
        ./common
        ./remote_client
        ./handlers
        ./remote_client/cache
	/opt/protobufstatic-3.0.0/include
	/opt/azure-storage-cpplite-0.3.0/include
)

file(GLOB STORAGE_INTERCEPT_SOURCES_COMMON ./common/*cpp ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./handlers/*cpp ./interceptors/StorageInterceptorImpl.cpp
        ./remote_client/cache/* ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common/*cpp)

file(GLOB STORAGE_INTERCEPT_SOURCES_INTERCEPTOR ${STORAGE_INTERCEPT_SOURCES_COMMON} ./interceptors/StorageInterceptor.cpp ./common/wrapper_impl/StorageLibcDlsymWrapper.cpp)
file(GLOB STORAGE_INTERCEPT_SOURCES_CACHE_CLIENT ${STORAGE_INTERCEPT_SOURCES_COMMON} ./interceptors/CacheClientInterface.cpp ./common/wrapper_impl/StorageLibcDirectWrapper.cpp)
# StorageIntercept Lib
add_library(StorageIntercept SHARED ${STORAGE_INTERCEPT_SOURCES_INTERCEPTOR})
add_library(CacheClient SHARED ${STORAGE_INTERCEPT_SOURCES_CACHE_CLIENT})

# Don't expose internally linked interfaces via the StorageIntercept shared library
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--exclude-libs,ALL")

# Statically link to precompiled protobuf and azurestorage sdk library to prevent mismatch with system installed versions
ADD_LIBRARY(staticprotobuf STATIC IMPORTED)
SET_TARGET_PROPERTIES(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)
ADD_LIBRARY(static-azurestorage STATIC IMPORTED)
SET_TARGET_PROPERTIES(static-azurestorage PROPERTIES
        IMPORTED_LOCATION /opt/azure-storage-cpplite-0.3.0/lib/libazure-storage-lite.a)
target_link_libraries(StorageIntercept diagnostics static-azurestorage curl jsoncpp dl staticprotobuf ibverbs)
target_link_libraries(CacheClient diagnostics static-azurestorage curl jsoncpp dl staticprotobuf ibverbs)

add_custom_command(
    TARGET CacheClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)

add_custom_command(
    TARGET CacheClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/interceptors/CacheClientInterface.h
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/include)

#StorageIntercept Unit Tests
enable_testing()

file(GLOB STORAGE_INTERCEPT_SOURCES_TEST ./common/*cpp ./common/wrapper_impl/StorageLibcDlsymWrapper.cpp 
        ./remote_client/*cpp ./remote_client/adls/*cpp
        ./remote_client/blob/*cpp ./remote_client/mock/*cpp ./handlers/*cpp ./remote_client/cache/* ./tests/*cpp
        ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common/*cpp)

add_executable(StorageInterceptUnitTests ${STORAGE_INTERCEPT_SOURCES_TEST})

target_compile_definitions(StorageInterceptUnitTests PRIVATE STORAGE_INTERCEPT_UT=1)

target_include_directories(StorageInterceptUnitTests PRIVATE
        ./adls
        ./common
        ./remote_client
        ./remote_client/adls
        ./remote_client/blob
        ./remote_client/mock
        ./remote_client/cache
        ./handlers
        ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common)

add_dependencies(StorageInterceptUnitTests
        diagnostics
        ${GMOCK_LIBRARY}
        ${GMOCK_MAIN_LIBRARY}
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
        )

target_link_libraries(StorageInterceptUnitTests
        diagnostics curl static-azurestorage jsoncpp dl uuid crypto staticprotobuf ibverbs
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
