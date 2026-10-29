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

project(storage_intercept_common LANGUAGES CXX C)
set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

add_library(StorageLibcDirectWrapper StorageLibcDirectWrapper.cpp)
add_library(StorageLibcDlsymWrapper StorageLibcDlsymWrapper.cpp)

set(IncludeDirs ${CMAKE_CURRENT_LIST_DIR})
set(Links StorageCommon diagnostics ${DACS_TLS_LIBS} jsoncpp dl uuid)
set(Dependencies diagnostics)

target_include_directories(StorageLibcDirectWrapper PUBLIC ${IncludeDirs})
target_include_directories(StorageLibcDlsymWrapper PUBLIC ${IncludeDirs})

add_dependencies(StorageLibcDirectWrapper ${Dependencies})
add_dependencies(StorageLibcDlsymWrapper ${Dependencies})

target_link_libraries(StorageLibcDirectWrapper ${Links})
target_link_libraries(StorageLibcDlsymWrapper ${Links})
