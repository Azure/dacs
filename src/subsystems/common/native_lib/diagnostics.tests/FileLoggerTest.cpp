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