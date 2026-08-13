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

cmake_minimum_required(VERSION 3.10)
project(HashingStrategy)

find_package(OpenSSL REQUIRED)

add_executable(HashingStrategyTest
    test/HashingStrategyTest.cpp
    test/HashingStrategyBenchmark.cpp
    strategies/StaticHashingStrategy.cpp
    strategies/ConsistentHashingStrategy.cpp
    strategies/RendezvousHashingStrategy.cpp
    hash_functions/HashFunctions.cpp
    hash_functions/MurmurHash3.cpp
    hash_functions/xxhash.c
)

target_include_directories(HashingStrategyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hash_functions
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/test
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
)

enable_testing()
add_test(NAME hashing_strategy_test COMMAND HashingStrategyTest)