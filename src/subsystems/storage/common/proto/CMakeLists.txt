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

project(cache_proto LANGUAGES CXX C)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_REMOTE_PUBLISH_DIRECTORY "${CMAKE_REMOTE_PUBLISH_DIRECTORY}-storage/storage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}-storage/storage")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}-storage/storage")

message("Current CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message("Current CMAKE_LIBRARY_OUTPUT_DIRECTORY from ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message("Current CMAKE_REMOTE_PUBLISH_DIRECTORY ${CMAKE_REMOTE_PUBLISH_DIRECTORY}")

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")

# Generate protobuf source files
execute_process(COMMAND mkdir -p ${CMAKE_CURRENT_LIST_DIR}/compiled)
execute_process(
    COMMAND /opt/protobufstatic-3.0.0/bin/protoc
                --proto_path=${CMAKE_CURRENT_LIST_DIR} 
                --cpp_out=${CMAKE_CURRENT_LIST_DIR}/compiled/
                ${CMAKE_CURRENT_LIST_DIR}/cache.proto)

include_directories(${CMAKE_CURRENT_LIST_DIR}/compiled
                    /opt/protobufstatic-3.0.0/include
                    )

add_library(staticprotobuf STATIC IMPORTED)

add_library(CacheProto STATIC ${CMAKE_CURRENT_LIST_DIR}/compiled/cache.pb.cc)

# protoc 3.0 output is third-party code and does not satisfy the project's
# conversion warnings, so treat it like a system dependency.
target_compile_options(CacheProto PRIVATE -Wno-conversion -Wno-sign-conversion
    $<$<CXX_COMPILER_ID:Clang>:-Wno-shorten-64-to-32>)

target_include_directories(CacheProto SYSTEM
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/compiled>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dacs/proto>)

set_target_properties(staticprotobuf PROPERTIES
        IMPORTED_LOCATION /opt/protobufstatic-3.0.0/lib/libprotobuf.a)

target_link_libraries(CacheProto
    PUBLIC
        $<BUILD_INTERFACE:staticprotobuf>
        $<INSTALL_INTERFACE:protobuf::libprotobuf>)

set_target_properties(CacheProto PROPERTIES
    EXPORT_NAME CacheProto)

add_library(DACS::CacheProto ALIAS CacheProto)

if(DACS_INSTALL_SDK)
    install(TARGETS CacheProto
        EXPORT DACSTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${CMAKE_CURRENT_LIST_DIR}/compiled/cache.pb.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dacs/proto)
endif()