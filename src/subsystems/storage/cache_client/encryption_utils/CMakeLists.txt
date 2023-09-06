project(encryption_utils LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

file(GLOB CLIENT_UTILS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

add_library(EncryptionUtils ${CLIENT_UTILS_SOURCES})

target_include_directories(EncryptionUtils PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_dependencies(EncryptionUtils StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto diagnostics)

target_link_libraries(EncryptionUtils StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto diagnostics curl jsoncpp dl uuid crypto)
