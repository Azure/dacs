//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
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