# Project name, build system and c++ standards used
cmake_minimum_required(VERSION 3.10)
project(DACSDependencies LANGUAGES CXX C)

# Dependency Output Folder
set(CMAKE_DEP_INSTALL_DIR $ENV{CMAKE_DEP_INSTALL_DIR})

set(CMAKE_BINARY_DIR ${CMAKE_DEP_INSTALL_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_DEP_INSTALL_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_DEP_INSTALL_DIR}/rt)

# Default standard in the project is C99 & C++23
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_C_STANDARD 99)
set(C_STANDARD_REQUIRED TRUE)

# Compile time symbol checking like check_cxx_symbol_exists(std::fopen "cstdio" HAVE_STD_FOPEN)
include(CheckCXXSymbolExists)

# Enable referencing git projects dependencies
include(ExternalProject)

# The GTest git hub project for testing and testing related vars
include(external/External_GTest.cmake)
include(GoogleTest)
include_directories(${GTEST_INCLUDE_DIRS})
include(CTest)

# The spdlog git hub project for logging and related vars
include(external/External_spdlog.cmake)
include_directories(${CMAKE_DEP_INSTALL_DIR}/include)

# serialization support through json. It supports json, bson, cbor etc.
include(external/External_json.cmake)
include_directories(SYSTEM ${PROJECT_BINARY_DIR}/include)

# include xxHash
include(external/External_xxHash.cmake)
include_directories(${CMAKE_DEP_INSTALL_DIR}/include)

# azure storage lite library to communicate with blob services
include(external/External_storagelite.cmake)
include_directories(SYSTEM ${PROJECT_BINARY_DIR}/include)

# google protobuf static library
include(external/External_protobufstatic.cmake)
include_directories(SYSTEM ${PROJECT_BINARY_DIR}/include)

# prometheus cpp
include(external/External_prometheus_cpp.cmake)
include_directories(SYSTEM ${PROJECT_BINARY_DIR}/include)
