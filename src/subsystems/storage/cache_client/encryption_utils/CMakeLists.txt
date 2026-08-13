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

project(encryption_utils LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC -Wno-deprecated-declarations")

file(GLOB CLIENT_UTILS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.cpp)

add_library(EncryptionUtils ${CLIENT_UTILS_SOURCES})

target_include_directories(EncryptionUtils PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_dependencies(EncryptionUtils StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto diagnostics)

target_link_libraries(EncryptionUtils StorageCommon StorageLibcDlsymWrapper ClientConnections CacheProto diagnostics curl jsoncpp dl uuid crypto)
