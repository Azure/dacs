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

set(DACS_STATIC_OPENSSL_PREFIX "${CMAKE_DEP_INSTALL_DIR}/tls/openssl" CACHE PATH
    "Prefix containing lib/libssl.a and lib/libcrypto.a built with no-shared -fPIC")

find_program(DACS_TLS_MAKE_EXECUTABLE NAMES gmake make)
if(NOT DACS_TLS_MAKE_EXECUTABLE)
    message(FATAL_ERROR "Building OpenSSL and curl requires make.")
endif()

ExternalProject_Add(
    dacs_openssl
    # OpenSSL 3.5.9 release commit: 45e844fa2a14ec92d146bd8f5778ac130b6625fb.
    URL "https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz"
    URL_HASH "SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"
    DOWNLOAD_DIR "${CMAKE_DEP_INSTALL_DIR}/tls/downloads"
    INSTALL_DIR "${DACS_STATIC_OPENSSL_PREFIX}"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
        <SOURCE_DIR>/Configure no-shared no-tests -fPIC
        --prefix=<INSTALL_DIR> --libdir=lib
    BUILD_COMMAND ${DACS_TLS_MAKE_EXECUTABLE} -j4
    INSTALL_COMMAND ${DACS_TLS_MAKE_EXECUTABLE} install_sw
    BUILD_BYPRODUCTS
        "${DACS_STATIC_OPENSSL_PREFIX}/lib/libssl.a"
        "${DACS_STATIC_OPENSSL_PREFIX}/lib/libcrypto.a"
)
