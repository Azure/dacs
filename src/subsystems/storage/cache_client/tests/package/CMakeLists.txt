cmake_minimum_required(VERSION 3.19)

project(DACSCacheClientPackageTest LANGUAGES CXX)

option(DACS_PACKAGE_TEST_STATIC "Test the static cache client package" OFF)

if(DACS_PACKAGE_TEST_STATIC)
    find_package(DACS CONFIG REQUIRED COMPONENTS Static)
else()
    find_package(DACS CONFIG REQUIRED)
endif()

add_executable(dacs_cache_client_shared_package_test main.cpp)
target_link_libraries(
    dacs_cache_client_shared_package_test
    PRIVATE DACS::CacheClientShared)

if(DACS_PACKAGE_TEST_STATIC)
    add_executable(dacs_cache_client_static_package_test main.cpp)
    target_link_libraries(
        dacs_cache_client_static_package_test
        PRIVATE DACS::CacheClient)
endif()
