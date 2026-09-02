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

get_filename_component(DACS_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DACS_SOURCE_ROOT "${DACS_SOURCE_ROOT}" CACHE PATH "DACS source root")

option(DACS_BUILD_CACHE_CLIENT "Build the DACS cache client" ON)
option(DACS_BUILD_CACHE_SERVER "Build the DACS cache server" ON)
option(DACS_BUILD_STORAGE_INTERCEPT "Build DACS storage interception libraries" ON)
option(DACS_BUILD_LIVENESS_PROBE "Build the DACS cache client liveness probe" ON)
option(DACS_BUILD_TESTS "Build DACS storage tests" ON)
option(DACS_BUILD_TOOLS "Build DACS storage tools" ON)

if(NOT TARGET diagnostics)
    add_subdirectory(${DACS_SOURCE_ROOT}/src/subsystems/common/native_lib/diagnostics diagnostics)
endif()
add_subdirectory(common/proto)
add_subdirectory(common/libc_wrapper)
add_subdirectory(cache_client/connection)
add_subdirectory(common/ibverbs_dlsym)
add_subdirectory(common)
add_subdirectory(cache_client/encryption_utils)
add_subdirectory(locator)

if(DACS_BUILD_CACHE_CLIENT)
    add_subdirectory(cache_client)
    if(DACS_BUILD_LIVENESS_PROBE)
        add_subdirectory(cache_client/liveness_probe)
    endif()
endif()

if(DACS_BUILD_STORAGE_INTERCEPT)
    add_subdirectory(storage_intercept)
endif()

if(DACS_BUILD_CACHE_SERVER)
    add_subdirectory(cache_server)
endif()
