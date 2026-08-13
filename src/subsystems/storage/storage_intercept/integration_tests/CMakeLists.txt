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

project(storage_intercept_integ_test LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

# Integration test for storage interception
add_executable(StorageInterceptIntegTest
    src/TestClient.cpp
)

add_dependencies(StorageInterceptIntegTest storage_utils)

target_link_libraries(StorageInterceptIntegTest storage_utils pthread)