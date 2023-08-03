project(client_utils LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

file(GLOB CLIENT_UTILS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/common/*.cpp)

add_library(ClientConnections ${CLIENT_UTILS_SOURCES})

target_include_directories(ClientConnections PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_SOURCE_DIR}/src/subsystems/storage/storage_intercept/common/)

add_dependencies(ClientConnections StorageCommon StorageInterceptLibcDlsym CacheProto diagnostics)

target_link_libraries(ClientConnections StorageCommon StorageInterceptLibcDlsym CacheProto diagnostics curl jsoncpp dl uuid crypto ibverbs)