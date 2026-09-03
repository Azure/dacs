cmake_minimum_required(VERSION 3.19)

project(DACSCacheClientPackageTest LANGUAGES CXX)

option(DACS_PACKAGE_TEST_STATIC "Test the static cache client package" OFF)
option(DACS_PACKAGE_TEST_STORAGE_REMOTE
    "Test the storage remote client package" OFF)

set(_dacs_package_components Proto Locator)
if(DACS_PACKAGE_TEST_STATIC)
    list(APPEND _dacs_package_components Static)
endif()
if(DACS_PACKAGE_TEST_STORAGE_REMOTE)
    list(APPEND _dacs_package_components StorageRemote)
endif()

find_package(DACS CONFIG REQUIRED
    COMPONENTS ${_dacs_package_components})

add_executable(dacs_cache_client_shared_package_test main.cpp)
target_link_libraries(
    dacs_cache_client_shared_package_test
    PRIVATE
        DACS::CacheClientShared
        DACS::CacheProto
        DACS::Locator)

if(DACS_PACKAGE_TEST_STATIC)
    add_executable(dacs_cache_client_static_package_test main.cpp)
    target_link_libraries(
        dacs_cache_client_static_package_test
        PRIVATE
            DACS::CacheClient
            DACS::CacheProto
            DACS::Locator)
endif()

if(DACS_PACKAGE_TEST_STORAGE_REMOTE)
    add_executable(dacs_storage_remote_package_test storage_remote.cpp)
    target_link_libraries(
        dacs_storage_remote_package_test
        PRIVATE DACS::StorageRemoteClient)
endif()
