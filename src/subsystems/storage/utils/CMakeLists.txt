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

# The storage_utils project with relevent dependency imports
project(storage_utils)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Files to build as the libstorage_utils
add_library(storage_utils 
    ThreadPool.cpp
    NetworkCounters.cpp)

target_include_directories(storage_utils INTERFACE
    ${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(storage_utils PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common)

# Wiring things up for compile and the runtime
target_link_libraries(storage_utils PRIVATE -L${DEP_INSTALL_DIR}/lib)
target_link_libraries(storage_utils PRIVATE diagnostics)
target_link_libraries(storage_utils PRIVATE Threads::Threads)

# Storage utils unit tests
enable_testing()

file(GLOB STORAGE_UTILS_TEST_SOURCES ./unittest/*cpp)

add_executable(StorageUtilsUnitTests ${STORAGE_UTILS_TEST_SOURCES})

target_include_directories(StorageUtilsUnitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/subsystems/common/native_lib/diagnostics
    ${CMAKE_SOURCE_DIR}/src/subsystems/storage/common
    ${CMAKE_CURRENT_SOURCE_DIR}
    ./unittest)

add_dependencies(StorageUtilsUnitTests
    storage_utils
    diagnostics
    ${GMOCK_LIBRARY}
    ${GMOCK_MAIN_LIBRARY}
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY})

target_link_libraries(StorageUtilsUnitTests
    storage_utils
    diagnostics
    Threads::Threads
    ${GMOCK_LIBRARY}
    ${GMOCK_MAIN_LIBRARY}
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    -L${DEP_INSTALL_DIR}/lib)

add_test(NAME StorageUtilsUnitTests COMMAND StorageUtilsUnitTests)
