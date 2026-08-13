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
        azurestoragelite
        # Pin the selected azure-storage-cpplite commit archive by SHA-256.
        URL "https://github.com/Azure/azure-storage-cpplite/archive/33a807a3984681a604c41659a4a2030ea81194e7.tar.gz"
        URL_HASH "SHA256=c781b7e6a3d7706657b024191cbaa35b7a06fce9b3d0287836eee321a9ada18f"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/azure-storage-cpplite-0.3.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
