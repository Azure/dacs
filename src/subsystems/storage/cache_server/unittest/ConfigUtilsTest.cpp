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

#include "ConfigUtils.h"
#include "Constants.h"
#include "IBConstants.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
class ConfigUtilsTest : public ::testing::Test
{
protected:
    static constexpr const char* EnvVarName = "DACS_UNITTEST_CONFIG_ENV_VAR";

    void SetUp() override
    {
        if (const char* value = std::getenv(EnvVarName))
        {
            m_originalValue = value;
        }
        ASSERT_EQ(0, ::unsetenv(EnvVarName));
    }

    void TearDown() override
    {
        if (m_originalValue.has_value())
        {
            EXPECT_EQ(0, ::setenv(EnvVarName, m_originalValue->c_str(), 1));
        }
        else
        {
            EXPECT_EQ(0, ::unsetenv(EnvVarName));
        }
    }

private:
    std::optional<std::string> m_originalValue;
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, BoolReturnsDefaultWhenUnset)
{
    EXPECT_FALSE(GetConfigEnvVar(EnvVarName, false));
    EXPECT_TRUE(GetConfigEnvVar(EnvVarName, true));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, BoolAcceptsOneAndCaseInsensitiveTrue)
{
    for (const char* value : {"1", "true", "TRUE", "True", "tRuE"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_TRUE(GetConfigEnvVar(EnvVarName, false));
        EXPECT_TRUE(GetConfigEnvVar(EnvVarName, true));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, BoolReturnsFalseForOtherValues)
{
    for (const char* value : {"", "0", "false", "FALSE", "yes", "on", "2", "01", "10", " 1", "1 ", " true", "true "})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_FALSE(GetConfigEnvVar(EnvVarName, false));
        EXPECT_FALSE(GetConfigEnvVar(EnvVarName, true));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, StringReturnsDefaultWhenUnset)
{
    EXPECT_EQ("fallback", GetConfigEnvVar(EnvVarName, "fallback"));
    EXPECT_EQ("", GetConfigEnvVar(EnvVarName, ""));
    EXPECT_EQ("fallback", GetConfigEnvVar(EnvVarName, std::string("fallback")));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, StringPreservesSetValuesIncludingEmpty)
{
    for (const char* value : {"", "1", "true", "hostname", "  raw value  "})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(value, GetConfigEnvVar(EnvVarName, "fallback"));
        EXPECT_EQ(value, GetConfigEnvVar(EnvVarName, std::string("fallback")));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntReturnsDefaultWhenUnset)
{
    EXPECT_EQ(256, GetConfigEnvVar(EnvVarName, 256, 1, 2048));
    EXPECT_EQ(-1, GetConfigEnvVar(EnvVarName, -1, -10, 10));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntParsesValuesAndBoundaries)
{
    for (int value : {std::numeric_limits<int>::min(), -1, 0, 256, std::numeric_limits<int>::max()})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(value).c_str(), 1));
        EXPECT_EQ(value, GetConfigEnvVar(
            EnvVarName, 123, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntPreservesExistingNumericPrefixParsing)
{
    ASSERT_EQ(0, ::setenv(EnvVarName, "  +42seconds", 1));
    EXPECT_EQ(42, GetConfigEnvVar(EnvVarName, 123, 1, 2048));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntThrowsForInvalidValues)
{
    for (const char* value : {"", "invalid"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_THROW(GetConfigEnvVar(EnvVarName, 123, 1, 2048), std::invalid_argument);
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntClampsToInclusiveBounds)
{
    for (int value : {0, 1, 2048, 2049})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(value).c_str(), 1));
        const int expected = value == 0 ? 1 : value == 2049 ? 2048 : value;
        EXPECT_EQ(expected, GetConfigEnvVar(EnvVarName, 256, 1, 2048));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntClampsValuesBeyondStorageRange)
{
    for (long long value : {static_cast<long long>(std::numeric_limits<int>::min()) - 1,
                            static_cast<long long>(std::numeric_limits<int>::max()) + 1})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(value).c_str(), 1));
        const int expected = value < 0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
        EXPECT_EQ(expected, GetConfigEnvVar(
            EnvVarName, 123, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, IntClampsExtremelyLargeMagnitudes)
{
    for (const char* value : {"9223372036854775808", "18446744073709551616", "  +999999999999999999999999"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(2048, GetConfigEnvVar(EnvVarName, 256, 1, 2048));
    }
    for (const char* value : {"-9223372036854775809", "-18446744073709551616", " \t-999999999999999999999999"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(1, GetConfigEnvVar(EnvVarName, 256, 1, 2048));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ReturnsDefaultWhenUnset)
{
    EXPECT_EQ(300u, GetConfigEnvVar(EnvVarName, uint32_t{300}, uint32_t{1}, uint32_t{7200}));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ParsesValuesAndBoundaries)
{
    for (uint32_t value : {uint32_t{0}, uint32_t{300}, std::numeric_limits<uint32_t>::max()})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(value).c_str(), 1));
        EXPECT_EQ(value, GetConfigEnvVar(
            EnvVarName, uint32_t{123}, uint32_t{0}, std::numeric_limits<uint32_t>::max()));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32PreservesExistingNumericPrefixParsing)
{
    ASSERT_EQ(0, ::setenv(EnvVarName, "  +300seconds", 1));
    EXPECT_EQ(300u, GetConfigEnvVar(EnvVarName, uint32_t{123}, uint32_t{1}, uint32_t{7200}));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ThrowsForInvalidValues)
{
    for (const char* value : {"", "invalid"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_THROW(
            GetConfigEnvVar(EnvVarName, uint32_t{123}, uint32_t{1}, uint32_t{7200}), std::invalid_argument);
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ClampsToInclusiveBounds)
{
    for (uint32_t value : {uint32_t{0}, uint32_t{1}, uint32_t{7200}, uint32_t{7201}})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(value).c_str(), 1));
        const uint32_t expected = value == 0 ? 1u : value == 7201 ? 7200u : value;
        EXPECT_EQ(expected, GetConfigEnvVar(EnvVarName, uint32_t{300}, uint32_t{1}, uint32_t{7200}));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ClampsValuesBeyondStorageRange)
{
    for (const char* value : {"-1", "4294967296", "18446744073709551616"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        const uint32_t expected = value[0] == '-' ? 0u : std::numeric_limits<uint32_t>::max();
        EXPECT_EQ(expected, GetConfigEnvVar(
            EnvVarName, uint32_t{123}, uint32_t{0}, std::numeric_limits<uint32_t>::max()));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, Uint32ClampsExtremelyLargeMagnitudes)
{
    for (const char* value : {"9223372036854775808", "18446744073709551616", "  +999999999999999999999999"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(7200u, GetConfigEnvVar(EnvVarName, uint32_t{300}, uint32_t{1}, uint32_t{7200}));
    }
    for (const char* value : {"-1", "-9223372036854775809", " \t-999999999999999999999999"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(1u, GetConfigEnvVar(EnvVarName, uint32_t{300}, uint32_t{1}, uint32_t{7200}));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, NumericRejectsInvalidBoundsAndDefaults)
{
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, 5, 10, 1), std::invalid_argument);
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, 0, 1, 10), std::invalid_argument);
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, 11, 1, 10), std::invalid_argument);
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, uint32_t{5}, uint32_t{10}, uint32_t{1}), std::invalid_argument);
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, uint32_t{0}, uint32_t{1}, uint32_t{10}), std::invalid_argument);
    EXPECT_THROW(GetConfigEnvVar(EnvVarName, uint32_t{11}, uint32_t{1}, uint32_t{10}), std::invalid_argument);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, NumericAcceptsEqualBounds)
{
    EXPECT_EQ(5, GetConfigEnvVar(EnvVarName, 5, 5, 5));
    EXPECT_EQ(5u, GetConfigEnvVar(EnvVarName, uint32_t{5}, uint32_t{5}, uint32_t{5}));
    for (const char* value : {"-1", "5", "6"})
    {
        SCOPED_TRACE(value);
        ASSERT_EQ(0, ::setenv(EnvVarName, value, 1));
        EXPECT_EQ(5, GetConfigEnvVar(EnvVarName, 5, 5, 5));
        EXPECT_EQ(5u, GetConfigEnvVar(EnvVarName, uint32_t{5}, uint32_t{5}, uint32_t{5}));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, ClampsConfiguredIntegerSettingsToRequestedRanges)
{
    const struct
    {
        const char* name;
        int defaultValue;
        int minValue;
        int maxValue;
        int expectedMin;
        int expectedMax;
    } settings[] = {
        {"CACHESERVER_THREADPOOL_SIZE", DEFAULT_THREAD_POOL_SIZE, MIN_THREAD_POOL_SIZE, MAX_THREAD_POOL_SIZE, 1, 2048},
        {"CACHESERVER_SOCKET_SNDBUF_SIZE", DEFAULT_SOCKET_BUF_SIZE, MIN_SOCKET_BUF_SIZE, MAX_SOCKET_BUF_SIZE, 0, 1073741824},
        {"CACHESERVER_SOCKET_RCVBUF_SIZE", DEFAULT_SOCKET_BUF_SIZE, MIN_SOCKET_BUF_SIZE, MAX_SOCKET_BUF_SIZE, 0, 1073741824},
        {"TEST_CACHESERVER_ADDR_PORT", 20000, 1, 65535, 1, 65535}};

    for (const auto& setting : settings)
    {
        SCOPED_TRACE(setting.name);
        ASSERT_EQ(0, ::unsetenv(EnvVarName));
        EXPECT_EQ(setting.defaultValue, GetConfigEnvVar(
            EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(setting.expectedMin - 1).c_str(), 1));
        EXPECT_EQ(setting.expectedMin, GetConfigEnvVar(
            EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(setting.expectedMax + 1).c_str(), 1));
        EXPECT_EQ(setting.expectedMax, GetConfigEnvVar(
            EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, ClampsConfiguredUnsignedSettingsToRequestedRanges)
{
    const struct
    {
        const char* name;
        uint32_t defaultValue;
        uint32_t minValue;
        uint32_t maxValue;
        uint32_t expectedMax;
    } settings[] = {
        {"CACHESERVER_IB_RECEIVE_BUFFERS", DEFAULT_RECEIVE_BUFFERS, MIN_IB_RECEIVE_BUFFERS, MAX_IB_RECEIVE_BUFFERS, 1024},
        {"CACHESERVER_IB_RECEIVE_THREADS", DEFAULT_IB_RECEIVE_THREADS, MIN_IB_RECEIVE_THREADS, MAX_IB_RECEIVE_THREADS, 2048},
        {"CACHESERVER_SNAPSHOT_INTERVAL", DefaultSnapshotIntervalInSeconds,
            MinSnapshotIntervalInSeconds, MaxSnapshotIntervalInSeconds, 7200}};

    for (const auto& setting : settings)
    {
        SCOPED_TRACE(setting.name);
        ASSERT_EQ(0, ::unsetenv(EnvVarName));
        EXPECT_EQ(setting.defaultValue, GetConfigEnvVar(
            EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
        ASSERT_EQ(0, ::setenv(EnvVarName, "0", 1));
        EXPECT_EQ(1u, GetConfigEnvVar(EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
        ASSERT_EQ(0, ::setenv(EnvVarName, std::to_string(setting.expectedMax + 1).c_str(), 1));
        EXPECT_EQ(setting.expectedMax, GetConfigEnvVar(
            EnvVarName, setting.defaultValue, setting.minValue, setting.maxValue));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConfigUtilsTest, ReadsCurrentValueRatherThanCaching)
{
    ASSERT_EQ(0, ::setenv(EnvVarName, "1", 1));
    EXPECT_TRUE(GetConfigEnvVar(EnvVarName, false));
    ASSERT_EQ(0, ::setenv(EnvVarName, "0", 1));
    EXPECT_FALSE(GetConfigEnvVar(EnvVarName, true));
    ASSERT_EQ(0, ::unsetenv(EnvVarName));
    EXPECT_TRUE(GetConfigEnvVar(EnvVarName, true));
}
//---------------------------------------------------------------------------------------------------------------------
