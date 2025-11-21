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