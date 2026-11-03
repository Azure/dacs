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

set(DACS_STATIC_CURL_PREFIX "${CMAKE_DEP_INSTALL_DIR}/tls/curl" CACHE PATH
    "Prefix containing lib/libcurl.a built against the owned static OpenSSL with -fPIC")

# Security updates require updating this upstream pin and rebuilding consumers.
ExternalProject_Add(
    dacs_curl
    DEPENDS dacs_openssl
    URL "https://curl.se/download/curl-8.23.0.tar.gz"
    URL_HASH "SHA256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49"
    DOWNLOAD_DIR "${CMAKE_DEP_INSTALL_DIR}/tls/downloads"
    INSTALL_DIR "${DACS_STATIC_CURL_PREFIX}"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" "CFLAGS=-fPIC"
        "PKG_CONFIG_PATH=${DACS_STATIC_OPENSSL_PREFIX}/lib/pkgconfig"
        <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --libdir=<INSTALL_DIR>/lib
        --disable-shared --enable-static --with-openssl=${DACS_STATIC_OPENSSL_PREFIX}
        --without-libssh2 --without-libpsl --without-libidn2 --without-nghttp2
        --without-brotli --without-zstd --without-librtmp --without-gssapi --without-libgsasl
    BUILD_COMMAND ${DACS_TLS_MAKE_EXECUTABLE} -j4
    INSTALL_COMMAND ${DACS_TLS_MAKE_EXECUTABLE} install
    BUILD_BYPRODUCTS "${DACS_STATIC_CURL_PREFIX}/lib/libcurl.a"
)
