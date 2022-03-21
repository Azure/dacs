//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "FileLogger.h"
#include <iostream>
#include "gtest/gtest.h"
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(FileLoggerTest, LogOutputToFileTest)
{
    FileLogger logger = FileLogger("./test.out", "./test.err");
    logger.LogOutput("output test line 1");
    logger.LogOutput("output test line 2");
    logger.LogError("error test");
    logger.Flush();
    logger.Shutdown();

    std::string fileContent;

    std::ifstream testOutFile("./test.out");
    std::getline (testOutFile, fileContent);
    ASSERT_EQ(fileContent, "output test line 1");
    std::getline (testOutFile, fileContent);
    ASSERT_EQ(fileContent, "output test line 2");

    std::ifstream testErrFile("./test.err");
    std::getline (testErrFile, fileContent);
    ASSERT_EQ(fileContent, "error test");
}
