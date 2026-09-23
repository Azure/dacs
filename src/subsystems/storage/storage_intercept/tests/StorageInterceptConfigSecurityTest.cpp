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

#include <gtest/gtest.h>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"

namespace
{

class StorageInterceptConfigSecurityTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        static int counter = 0;
        configPath = "/tmp/si_config_security_" +
            std::to_string(getpid()) + "_" + std::to_string(counter++);
        symlinkPath = configPath + ".symlink";

        const char* currentPath =
            getenv(StorageInterceptConstants::ConfigFilePathEnv);
        hadConfigPath = currentPath != nullptr;
        savedConfigPath = currentPath == nullptr ? "" : currentPath;
    }

    void TearDown() override
    {
        if (hadConfigPath)
        {
            setenv(
                StorageInterceptConstants::ConfigFilePathEnv,
                savedConfigPath.c_str(),
                1);
        }
        else
        {
            unsetenv(StorageInterceptConstants::ConfigFilePathEnv);
        }

        try
        {
            StorageInterceptConfig::GetInstance().ReloadConfig();
        }
        catch (...)
        {
        }

        unlink(symlinkPath.c_str());
        unlink(configPath.c_str());
    }

    void WriteConfig(mode_t mode)
    {
        int fd = open(
            configPath.c_str(),
            O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
            mode);
        ASSERT_GE(fd, 0);
        const char config[] = "storagePath /mnt/cache\n";
        ASSERT_EQ(
            write(fd, config, sizeof(config) - 1),
            static_cast<ssize_t>(sizeof(config) - 1));
        ASSERT_EQ(close(fd), 0);
        ASSERT_EQ(chmod(configPath.c_str(), mode), 0);
    }

    bool ReloadConfigAt(const std::string& path)
    {
        setenv(
            StorageInterceptConstants::ConfigFilePathEnv,
            path.c_str(),
            1);
        try
        {
            StorageInterceptConfig::GetInstance().ReloadConfig();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ReloadConfig()
    {
        return ReloadConfigAt(configPath);
    }

    std::string configPath;
    std::string symlinkPath;
    bool hadConfigPath = false;
    std::string savedConfigPath;
};

TEST_F(StorageInterceptConfigSecurityTest, LoadsOwnerOnlyRegularFile)
{
    WriteConfig(0600);
    EXPECT_TRUE(ReloadConfig());
}

TEST_F(StorageInterceptConfigSecurityTest, RejectsSymlink)
{
    WriteConfig(0600);
    ASSERT_EQ(symlink(configPath.c_str(), symlinkPath.c_str()), 0);
    EXPECT_FALSE(ReloadConfigAt(symlinkPath));
}

TEST_F(StorageInterceptConfigSecurityTest, LoadsNonOwnerOnlyRegularFile)
{
    WriteConfig(0644);
    EXPECT_TRUE(ReloadConfig());
}

} // namespace
