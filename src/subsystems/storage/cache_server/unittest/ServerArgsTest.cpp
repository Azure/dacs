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
//
//---------------------------------------------------------------------------------------------------------------------
#include "ServerArgs.h"
#include "Constants.h"
#include <gtest/gtest.h>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    // Helper to build an argv-style array (with argv[0] as program name) from a list of tokens.
    class ArgvBuilder
    {
    public:
        explicit ArgvBuilder(std::vector<std::string> tokens)
            : m_args({"CacheServer"})
        {
            for (auto& token : tokens)
            {
                m_args.push_back(std::move(token));
            }
            for (auto& arg : m_args)
            {
                m_argv.push_back(const_cast<char*>(arg.c_str()));
            }
        }

        int Argc() const { return static_cast<int>(m_argv.size()); }
        char** Argv() { return m_argv.data(); }

    private:
        std::vector<std::string> m_args;
        std::vector<char*> m_argv;
    };
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, ParsesAllNamedArguments)
{
    ArgvBuilder builder({"--port", "9065", "--cacheRoot", "/tmp/cache", "--sizeLimitBytes", "19903423432",
        "--ttlWindowSeconds", "120"});
    ServerArgs args;
    std::string usageMessage;
    ASSERT_EQ(ParseServerArgsResult::Success, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_EQ(9065, args.port);
    EXPECT_EQ("/tmp/cache", args.cacheRootPath);
    EXPECT_EQ(19903423432ull, args.cacheSizeLimitBytes);
    EXPECT_EQ(120u, args.cacheTTLWindowSeconds);
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, AppliesDefaultsWhenOnlyPortIsGiven)
{
    ArgvBuilder builder({"--port", "9065"});
    ServerArgs args;
    std::string usageMessage;
    ASSERT_EQ(ParseServerArgsResult::Success, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_EQ(9065, args.port);
    EXPECT_EQ(".", args.cacheRootPath);
    EXPECT_EQ(DefaultCacheSizeLimitBytes, args.cacheSizeLimitBytes);
    EXPECT_EQ(DefaultCacheTTLWindowSizeSeconds, args.cacheTTLWindowSeconds);
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, ArgumentsCanBeReordered)
{
    ArgvBuilder builder({"--sizeLimitBytes", "500", "--port", "9065"});
    ServerArgs args;
    std::string usageMessage;
    ASSERT_EQ(ParseServerArgsResult::Success, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_EQ(9065, args.port);
    EXPECT_EQ(500ull, args.cacheSizeLimitBytes);
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, MissingPortReturnsError)
{
    ArgvBuilder builder({"--cacheRoot", "/tmp/cache"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_FALSE(usageMessage.empty());
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, NoArgumentsReturnsError)
{
    ArgvBuilder builder({});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, InvalidPortValueReturnsError)
{
    ArgvBuilder builder({"--port", "not-a-number"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, UnknownFlagReturnsError)
{
    ArgvBuilder builder({"--port", "9065", "--unknownFlag", "1"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, ZeroPortReturnsError)
{
    ArgvBuilder builder({"--port", "0"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, NegativePortReturnsError)
{
    ArgvBuilder builder({"--port", "-1"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, PortAboveMaxReturnsError)
{
    ArgvBuilder builder({"--port", "70000"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, MaxValidPortIsAccepted)
{
    ArgvBuilder builder({"--port", "65535"});
    ServerArgs args;
    std::string usageMessage;
    ASSERT_EQ(ParseServerArgsResult::Success, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_EQ(65535, args.port);
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, ZeroSizeLimitBytesReturnsError)
{
    ArgvBuilder builder({"--port", "9065", "--sizeLimitBytes", "0"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::Error, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
}
//---------------------------------------------------------------------------------------------------------------------
TEST(ServerArgsTest, HelpFlagReturnsHelpRequested)
{
    ArgvBuilder builder({"--help"});
    ServerArgs args;
    std::string usageMessage;
    EXPECT_EQ(ParseServerArgsResult::HelpRequested, ParseServerArgs(builder.Argc(), builder.Argv(), args, usageMessage));
    EXPECT_FALSE(usageMessage.empty());
}
//---------------------------------------------------------------------------------------------------------------------
