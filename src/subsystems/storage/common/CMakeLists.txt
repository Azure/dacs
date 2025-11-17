project(StorageCommon LANGUAGES CXX C)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

file(GLOB CLIENT_UTILS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/checksum_util/*.cpp)

add_library(StorageCommon ${CLIENT_UTILS_SOURCES})

target_include_directories(StorageCommon PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/checksum_util)

add_dependencies(StorageCommon IbverbsDlsym diagnostics)

target_link_libraries(StorageCommon IbverbsDlsym diagnostics curl jsoncpp dl uuid crypto xxhash)