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
        MurmurHash3
        URL "https://codeload.github.com/aappleby/smhasher/tar.gz/92cf3702fcfaadc84eb7bef59825a23e0cd84f56"
        URL_HASH "SHA256=92492c02a06b472ff519a3a38318b3a6a0122401a4cad729b98207ac3514f011"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
                ${CMAKE_CXX_COMPILER} -O3 -fPIC -I<SOURCE_DIR>/src
                -c <SOURCE_DIR>/src/MurmurHash3.cpp
                -o <BINARY_DIR>/MurmurHash3.o
        COMMAND
                ${CMAKE_AR} qc <BINARY_DIR>/libMurmurHash3.a
                <BINARY_DIR>/MurmurHash3.o
        COMMAND
                ${CMAKE_RANLIB} <BINARY_DIR>/libMurmurHash3.a
        INSTALL_COMMAND
                ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_DEP_INSTALL_DIR}/include
        COMMAND
                ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_DEP_INSTALL_DIR}/lib
        COMMAND
                ${CMAKE_COMMAND} -E copy
                <SOURCE_DIR>/src/MurmurHash3.h
                ${CMAKE_DEP_INSTALL_DIR}/include/MurmurHash3.h
        COMMAND
                ${CMAKE_COMMAND} -E copy
                <BINARY_DIR>/libMurmurHash3.a
                ${CMAKE_DEP_INSTALL_DIR}/lib/libMurmurHash3.a
        TEST_COMMAND ""
)
