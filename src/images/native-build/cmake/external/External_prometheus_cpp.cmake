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
        prometheus_cpp
        # Pin the official prometheus-cpp 1.3.0 archive with submodules by SHA-256.
        URL "https://github.com/jupp0r/prometheus-cpp/releases/download/v1.3.0/prometheus-cpp-with-submodules.tar.gz"
        URL_HASH "SHA256=62bc2cc9772db2314dbaae506ae2a75c8ee897dab053d8729e86a637b018fdb6"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS 
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_INSTALL_PREFIX=${DEP_INSTALL_DIR}
                -DENABLE_PUSH=OFF
                -DENABLE_COMPRESSION=OFF
        TEST_COMMAND ""
)
