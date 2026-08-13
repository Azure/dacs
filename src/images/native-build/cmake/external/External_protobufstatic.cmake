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
        protobufstatic
        # Pin the official protobuf C++ 3.21.12 release archive by SHA-256.
        URL "https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz"
        URL_HASH "SHA256=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460"
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=/opt/protobufstatic-3.0.0
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -Dprotobuf_BUILD_TESTS=OFF
                -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations"
)
