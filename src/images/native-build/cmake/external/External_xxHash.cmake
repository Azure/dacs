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

ExternalProject_Add(
        xxHash
        # Pin the xxHash 0.8.3 tag archive by SHA-256.
        URL "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz"
        URL_HASH "SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${CMAKE_DEP_INSTALL_DIR}
        TEST_COMMAND ""
        SOURCE_SUBDIR "cmake_unofficial"
)

# line 13 : This repo has CMakeLists.txt in /cmake_unofficial - not root
