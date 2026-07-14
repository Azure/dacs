//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "FileLogger.h"
#include <experimental/filesystem>
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
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(FileLoggerTest, FileNameWithRankAvailable)
{
    char envRank[] = "RANK=0";
    putenv(envRank);
    FileLogger logger = FileLogger("./test{{RANK}}.out", "./test{{RANK}}.err");
    logger.Shutdown();

    ASSERT_TRUE(std::experimental::filesystem::exists("./test0.out"));
    ASSERT_TRUE(std::experimental::filesystem::exists("./test0.err"));
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(FileLoggerTest, FileNameWithNoRankAvailable)
{
    FileLogger logger = FileLogger("./test{{RANK}}.out", "./test{{RANK}}.err");
    logger.Shutdown();

    const std::string strPid = std::to_string(::getpid());
    ASSERT_TRUE(std::experimental::filesystem::exists("./test" + strPid + ".out"));
    ASSERT_TRUE(std::experimental::filesystem::exists("./test" + strPid + ".err"));
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(FileLoggerTest, FileNameWithIgnoreRank)
{
    FileLogger logger = FileLogger("./test{{RANK}}.out", "./test{{RANK}}.err", /*bIgnoreRankInFileName*/ true);
    logger.Shutdown();

    ASSERT_TRUE(std::experimental::filesystem::exists("./test.out"));
    ASSERT_TRUE(std::experimental::filesystem::exists("./test.err"));
}
//--------------------------------------------------------------------------------------------------------------------------------------------