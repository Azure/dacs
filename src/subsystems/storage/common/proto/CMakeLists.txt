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

set(_cache_proto_output_dir "${CMAKE_CURRENT_BINARY_DIR}/compiled")
set(_cache_proto_source "${_cache_proto_output_dir}/cache.pb.cc")
set(_cache_proto_header "${_cache_proto_output_dir}/cache.pb.h")

add_custom_command(
    OUTPUT "${_cache_proto_source}" "${_cache_proto_header}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_cache_proto_output_dir}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
        --proto_path=${CMAKE_CURRENT_LIST_DIR}
        --cpp_out=${_cache_proto_output_dir}
        "${CMAKE_CURRENT_LIST_DIR}/cache.proto"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/cache.proto" protobuf::protoc
    VERBATIM)

add_library(CacheProto STATIC "${_cache_proto_source}")
set_target_properties(CacheProto PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# protoc 3.0 output is third-party code and does not satisfy the project's
# conversion warnings, so treat it like a system dependency.
target_compile_options(CacheProto PRIVATE -Wno-conversion -Wno-sign-conversion
    $<$<CXX_COMPILER_ID:Clang>:-Wno-shorten-64-to-32>)

target_include_directories(CacheProto SYSTEM
    PUBLIC
        $<BUILD_INTERFACE:${_cache_proto_output_dir}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dacs/proto>)

target_link_libraries(CacheProto
    PUBLIC protobuf::libprotobuf)

set_target_properties(CacheProto PROPERTIES
    EXPORT_NAME CacheProto)

add_library(DACS::CacheProto ALIAS CacheProto)

if(DACS_INSTALL_SDK)
    install(TARGETS CacheProto
        EXPORT DACSTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES "${_cache_proto_header}"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/proto)
endif()