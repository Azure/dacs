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

# The diagnostics project with relevent dependency imports
project(diagnostics)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Files to build as the libdiagnostics
add_library(diagnostics 
    FileLogger.cpp
    StandardLogger.cpp
    LoggingApi.cpp)

set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS_INIT} -fPIC")
# Wiring things up for compile and the runtime
target_link_libraries(diagnostics PRIVATE -L${DEP_INSTALL_DIR}/lib)
target_link_libraries(diagnostics PRIVATE Threads::Threads)
target_include_directories(diagnostics
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>)
set_target_properties(diagnostics PROPERTIES
    EXPORT_NAME Diagnostics)

if(DACS_INSTALL_SDK)
    install(TARGETS diagnostics
        EXPORT DACSTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
