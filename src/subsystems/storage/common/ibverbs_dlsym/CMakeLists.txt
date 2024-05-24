project(client_connections LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

file(GLOB CLIENT_UTILS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

add_library(IbverbsDlsym ${CLIENT_UTILS_SOURCES})

target_include_directories(IbverbsDlsym PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_dependencies(IbverbsDlsym diagnostics)

target_link_libraries(IbverbsDlsym diagnostics)