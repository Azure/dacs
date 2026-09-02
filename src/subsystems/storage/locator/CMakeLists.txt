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

add_library(DACSLocator STATIC
    FlatMapIndex.cpp
    FlatRadixIndex.cpp
    LocatorCache.cpp
)

target_include_directories(DACSLocator PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(DACSLocator PUBLIC pthread PRIVATE diagnostics)

add_library(DACS::Locator ALIAS DACSLocator)
