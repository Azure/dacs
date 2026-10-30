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

# Default static TLS stack for the storage client targets (StorageDirect,
# StorageIntercept, CacheClient and their StorageCommon/ClientConnections/
# EncryptionUtils/libc_wrapper dependencies). When enabled, curl and OpenSSL
# are linked from prebuilt no-shared/-fPIC archives instead of the system
# libcurl.so/libssl.so/libcrypto.so, so the resulting shared libraries do not
# carry a dynamic OpenSSL/curl dependency. Stage 1 builds pinned OpenSSL 3.5.9
# and upstream curl 7.81.0 into ${CMAKE_DEP_INSTALL_DIR}/tls/{openssl,curl}:
#
#   export CMAKE_DEP_INSTALL_DIR=/dependency
#   cmake -S src/images/native-build/cmake -B build/tls-dependencies -G Ninja
#   cmake --build build/tls-dependencies --target dacs_curl --parallel 4
#
# Then configure the application with its normal output environment.
# Stage 1 must finish before Stage 2 configuration:
# Azure SDK and OpenTelemetry discover TLS headers/libraries at configure time.
# Advanced overrides DACS_STATIC_OPENSSL_PREFIX and DACS_STATIC_CURL_PREFIX
# select alternative prebuilt PIC archive installations.
#
# DACS_STATIC_TLS defaults to ON. Set -DDACS_STATIC_TLS=OFF to link the system
# shared libcurl/libssl/libcrypto packages installed by the native-build image.
# Upstream curl 7.81.0 is NOT Ubuntu's security-patched package. This static
# mode is not production security ready; resolve patch coverage before adoption.
option(DACS_STATIC_TLS
    "Link storage client targets against static, position-independent OpenSSL/curl archives instead of system shared libcurl/libssl/libcrypto"
    ON)

if(DACS_STATIC_TLS)
    set(DACS_STATIC_OPENSSL_PREFIX "${CMAKE_DEP_INSTALL_DIR}/tls/openssl" CACHE PATH
        "Prefix containing lib/libssl.a and lib/libcrypto.a built with no-shared -fPIC")
    set(DACS_STATIC_CURL_PREFIX "${CMAKE_DEP_INSTALL_DIR}/tls/curl" CACHE PATH
        "Prefix containing lib/libcurl.a built --disable-shared against DACS_STATIC_OPENSSL_PREFIX with -fPIC")

    mark_as_advanced(DACS_STATIC_OPENSSL_PREFIX DACS_STATIC_CURL_PREFIX)
    foreach(archive
        "${DACS_STATIC_OPENSSL_PREFIX}/lib/libssl.a"
        "${DACS_STATIC_OPENSSL_PREFIX}/lib/libcrypto.a"
        "${DACS_STATIC_CURL_PREFIX}/lib/libcurl.a")
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR
                "Missing static TLS archive: ${archive}. Build the Stage 1 dacs_curl target "
                "before configuring DACS_STATIC_TLS=ON (see static_tls.cmake).")
        endif()
    endforeach()

    # Find modules still populate legacy variables even when targets exist.
    # Seed their probes so SDK source and headers use the same owned stack.
    set(OPENSSL_ROOT_DIR "${DACS_STATIC_OPENSSL_PREFIX}")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(OPENSSL_INCLUDE_DIR "${DACS_STATIC_OPENSSL_PREFIX}/include")
    set(OPENSSL_SSL_LIBRARY "${DACS_STATIC_OPENSSL_PREFIX}/lib/libssl.a")
    set(OPENSSL_CRYPTO_LIBRARY "${DACS_STATIC_OPENSSL_PREFIX}/lib/libcrypto.a")
    set(CURL_INCLUDE_DIR "${DACS_STATIC_CURL_PREFIX}/include")
    set(CURL_LIBRARY "${DACS_STATIC_CURL_PREFIX}/lib/libcurl.a")
    find_package(Threads REQUIRED)

    add_library(DacsStaticOpenSSL::Crypto STATIC IMPORTED)
    set_target_properties(DacsStaticOpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${DACS_STATIC_OPENSSL_PREFIX}/lib/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_OPENSSL_PREFIX}/include"
        INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")

    add_library(DacsStaticOpenSSL::SSL STATIC IMPORTED)
    set_target_properties(DacsStaticOpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${DACS_STATIC_OPENSSL_PREFIX}/lib/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_OPENSSL_PREFIX}/include"
        INTERFACE_LINK_LIBRARIES DacsStaticOpenSSL::Crypto)

    add_library(DacsStaticCurl::libcurl STATIC IMPORTED)
    set_target_properties(DacsStaticCurl::libcurl PROPERTIES
        IMPORTED_LOCATION "${DACS_STATIC_CURL_PREFIX}/lib/libcurl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_CURL_PREFIX}/include"
        # curl's static archive calls zlib directly. Keep zlib dynamic.
        INTERFACE_LINK_LIBRARIES "DacsStaticOpenSSL::SSL;DacsStaticOpenSSL::Crypto;z")

    # Replaces the bare "curl" and "crypto" linker names used throughout the
    # storage client CMakeLists.txt files. Order matters: curl's static
    # archive must come before the OpenSSL archives that satisfy its symbols.
    set(DACS_TLS_LIBS DacsStaticCurl::libcurl DacsStaticOpenSSL::SSL DacsStaticOpenSSL::Crypto)

    # Azure SDK for C++ (azure-core) calls find_package(CURL REQUIRED) and
    # find_package(OpenSSL REQUIRED) internally for its curl HTTP transport
    # and SHA hashing. Predefining their canonical targets before the
    # OpenTelemetry config and azure-sdk-cpp.cmake run makes them link the same
    # static archives instead of system shared libcurl/libssl/libcrypto.
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl STATIC IMPORTED)
        set_target_properties(CURL::libcurl PROPERTIES
            IMPORTED_LOCATION "${DACS_STATIC_CURL_PREFIX}/lib/libcurl.a"
            INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_CURL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES "DacsStaticOpenSSL::SSL;DacsStaticOpenSSL::Crypto;z")
    endif()
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${DACS_STATIC_OPENSSL_PREFIX}/lib/libcrypto.a"
            INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES "Threads::Threads;${CMAKE_DL_LIBS}")
    endif()
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${DACS_STATIC_OPENSSL_PREFIX}/lib/libssl.a"
            INTERFACE_INCLUDE_DIRECTORIES "${DACS_STATIC_OPENSSL_PREFIX}/include"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
    endif()
else()
    set(DACS_TLS_LIBS curl crypto)
endif()
