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
        fmt
        # Pin the official fmt 9.1.0 release archive by SHA-256.
        URL "https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip"
        URL_HASH "SHA256=cceb4cb9366e18a5742128cb3524ce5f50e88b476f1e54737a47ffdf4df4c996"
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_DEP_INSTALL_DIR}
        -DFMT_DOC=OFF
        -DFMT_TEST=OFF
        TEST_COMMAND ""
)
