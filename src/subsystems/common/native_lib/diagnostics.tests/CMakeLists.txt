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

project(diagnostics.tests)

# Add test discovery and execution
enable_testing()
add_executable(diagnostics_tests
        LoggingApiTest.cpp
        FileLoggerTest.cpp
        TestMain.cpp)

target_include_directories(diagnostics_tests PRIVATE  ../diagnostics)
target_include_directories(diagnostics_tests PRIVATE ${DEP_INSTALL_DIR}/include)
add_dependencies(diagnostics_tests diagnostics ${GMOCK_LIBRARY} ${GMOCK_MAIN_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY})
target_link_libraries(diagnostics_tests diagnostics -lstdc++fs  ${GMOCK_LIBRARY} ${GMOCK_MAIN_LIBRARY} ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY})
add_test(NAME diagnostics_tests COMMAND diagnostics_tests)