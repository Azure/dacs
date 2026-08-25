# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.19)
project(HashingStrategy)

find_package(OpenSSL REQUIRED)

set(CMAKE_DEP_INSTALL_DIR $ENV{CMAKE_DEP_INSTALL_DIR})
if(NOT CMAKE_DEP_INSTALL_DIR)
    message(FATAL_ERROR
        "CMAKE_DEP_INSTALL_DIR is not set. Build this target in the DACS native-build container.")
endif()

find_path(
    MURMURHASH3_INCLUDE_DIR
    MurmurHash3.h
    PATHS ${CMAKE_DEP_INSTALL_DIR}/include
    REQUIRED
    NO_DEFAULT_PATH
)
find_library(
    MURMURHASH3_LIBRARY
    NAMES MurmurHash3
    PATHS ${CMAKE_DEP_INSTALL_DIR}/lib
    REQUIRED
    NO_DEFAULT_PATH
)
find_path(
    XXHASH_INCLUDE_DIR
    xxhash.h
    PATHS ${CMAKE_DEP_INSTALL_DIR}/include
    REQUIRED
    NO_DEFAULT_PATH
)
find_library(
    XXHASH_LIBRARY
    NAMES xxhash
    PATHS ${CMAKE_DEP_INSTALL_DIR}/lib
    REQUIRED
    NO_DEFAULT_PATH
)

add_executable(HashingStrategyTest
    test/HashingStrategyTest.cpp
    test/HashingStrategyBenchmark.cpp
    strategies/StaticHashingStrategy.cpp
    strategies/ConsistentHashingStrategy.cpp
    strategies/RendezvousHashingStrategy.cpp
    hash_functions/HashFunctions.cpp
)

target_include_directories(HashingStrategyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hash_functions
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/test
    ${MURMURHASH3_INCLUDE_DIR}
    ${XXHASH_INCLUDE_DIR}
)

target_compile_features(HashingStrategyTest PRIVATE cxx_std_17)
target_compile_options(HashingStrategyTest PRIVATE 
    -Wall 
    -Wextra 
    -Wno-implicit-fallthrough
    -O2
)

target_link_libraries(HashingStrategyTest PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto
    ${MURMURHASH3_LIBRARY}
    ${XXHASH_LIBRARY}
)

enable_testing()
add_test(NAME hashing_strategy_test COMMAND HashingStrategyTest)