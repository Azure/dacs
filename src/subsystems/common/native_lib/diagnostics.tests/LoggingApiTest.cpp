// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file implements the LoggingApiTest
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "LoggingApi.h"
#include <iostream>
#include "gtest/gtest.h"
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(LoggingApiTest, LoggingMacroCompilationTest)
{
    LOG_TRACE("Testing {}", "LOG_TRACE");
    LOG_DEBUG("Testing {}", "LOG_DEBUG");
    LOG_INFO("Testing {}", "LOG_INFO");
    LOG_WARN("Testing {}", "LOG_WARN");
    LOG_ERROR("Testing {}", "LOG_ERROR");
    LOG_CRITICAL("Testing {}", "LOG_CRITICAL");
    CX_LOG_TRACE("Testing {}", "CX_LOG_TRACE");
    CX_LOG_DEBUG("Testing {}", "CX_LOG_DEBUG");
    CX_LOG_INFO("Testing {}", "CX_LOG_INFO");
    CX_LOG_WARN("Testing {}", "CX_LOG_WARN");
    CX_LOG_ERROR("Testing {}", "CX_LOG_ERROR");
    CX_LOG_CRITICAL("Testing {}", "CX_LOG_CRITICAL");
    LOG_STACK("Testing {}", "LOG_FAIL");
}