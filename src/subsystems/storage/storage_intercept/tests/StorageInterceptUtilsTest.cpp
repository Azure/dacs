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

// This file implements the StorageInterceptUtilsTest.cpp
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtilsTest.h"
#include "StorageInterceptConstants.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <string>
#include <vector>
//--------------------------------------------------------------------------------------------------------------------------------------------
StorageInterceptUtilsTest::StorageInterceptUtilsTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptUtilsTest::SetUp()
{

}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, CombinePathTest)
{
    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo", "bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", "bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", "/bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo", "/bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", ""),
        "foo");
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, GetRelativePathTest)
{
    std::string result;

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo/bar", result),
        true);
    EXPECT_EQ(result, "bar");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo/bar/aa/bb", result),
        true);
    EXPECT_EQ(result, "aa/bb");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo/", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foot/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/aa/foo/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo//bar//aa", result),
        true);
    EXPECT_EQ(result, "bar/aa");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bar", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bar//aa/bb//cc", result),
        true);
    EXPECT_EQ(result, "aa/bb/cc");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bart", result),
        false);
    EXPECT_EQ(result, "");
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, GetRemotePathPrefixBlobTest)
{
    std::string result;

    if (!m_bUsingMock)
    {
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "false");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobStorageAccName, "acc");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobContainerName, "cont");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "acc/cont/");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "$accountNameOverride$/cont/");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "false");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "acc/");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "$accountNameOverride$/");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "false");        
    }
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, IsAllowedBlobHostTest)
{
    // Legitimate Azure blob endpoints across clouds.
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("myacct.blob.core.windows.net"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("MyAcct.Blob.Core.Windows.Net"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.usgovcloudapi.net"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.chinacloudapi.cn"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.microsoft.scloud"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.eaglex.ic.gov"));

    // Attacker-controlled or malformed hosts. These assertions must not depend on the process
    // environment: IsAllowedBlobHost also honors AZURE_BLOB_ENDPOINT_SUFFIX, and a stray/broad
    // value (e.g. "com") in the test process would make "attacker.com" accepted. Clear it here so
    // this test always exercises the built-in allowlist regardless of environment state, and
    // restore whatever was there afterwards.
    const char* savedSuffix = std::getenv("AZURE_BLOB_ENDPOINT_SUFFIX");
    const bool hadSuffix = (savedSuffix != nullptr);
    const std::string savedSuffixValue = hadSuffix ? savedSuffix : "";
    unsetenv("AZURE_BLOB_ENDPOINT_SUFFIX");

    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("attacker.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.windows.net.attacker.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("attacker.com/blob.core.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.windows.net:8080"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("user@acct.blob.core.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost(".blob.core.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost(""));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedBlobHost("acct.blob.core.windows.net\n.attacker.com"));

    if (hadSuffix)
    {
        setenv("AZURE_BLOB_ENDPOINT_SUFFIX", savedSuffixValue.c_str(), 1);
    }
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, IsAllowedEndpointHostSuffixMatchTest)
{
    // A mixed-case suffix must match a mixed-case host: neither side is assumed lowercase.
    const std::vector<std::string> suffixes = { ".Blob.Core.Windows.NET" };
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedEndpointHost("acct.blob.core.windows.net", suffixes));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedEndpointHost("ACCT.BLOB.CORE.WINDOWS.NET", suffixes));

    // The suffix alone is not a host: a non-empty label must precede it.
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost(".blob.core.windows.net", suffixes));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("blob.core.windows.net", suffixes));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("acct..blob.core.windows.net", suffixes));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost(".acct.blob.core.windows.net", suffixes));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("acct.blob.core.windows.net.", suffixes));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("acct.blob..example.test", {".blob..example.test"}));

    // A shorter host can never match a longer suffix.
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("windows.net", suffixes));

    // An empty suffix must not turn the allowlist into a wildcard.
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("attacker.com", { "" }));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("attacker.com", {}));

    // Entries without a leading '.' are exact hostnames, not suffixes: no subdomain is admitted.
    const std::vector<std::string> exactEntries = { "login.windows.net" };
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedEndpointHost("login.windows.net", exactEntries));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedEndpointHost("LOGIN.WINDOWS.NET", exactEntries));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("evil.login.windows.net", exactEntries));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("login.windows.net.attacker.com", exactEntries));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedEndpointHost("windows.net", exactEntries));
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, IsAllowedAuthorityUrlTest)
{
    // Public and sovereign Azure AD authorities.
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.microsoftonline.com"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.microsoftonline.us"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.chinacloudapi.cn"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.microsoftonline.com/"));

    // Legacy single-host AAD authorities, matched exactly rather than as a ".windows.net"
    // suffix so unrelated *.windows.net services are not admitted.
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.windows.net"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://sts.windows.net"));
    EXPECT_TRUE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://Login.Windows.NET"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://database.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://management.core.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://evil.login.windows.net"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://windows.net"));

    // Wrong scheme, wrong host, or embedded credentials/paths.
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("http://login.microsoftonline.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://attacker.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://login.microsoftonline.com.attacker.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://attacker.com/login.microsoftonline.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl("https://attacker.com@login.microsoftonline.com"));
    EXPECT_FALSE(StorageInterceptUtils::IsAllowedAuthorityUrl(""));
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, LooksLikeJwtTest)
{
    EXPECT_TRUE(StorageInterceptUtils::LooksLikeJwt("header.payload.signature"));
    EXPECT_TRUE(StorageInterceptUtils::LooksLikeJwt("aGVsbG8=.d29ybGQ=.c2ln"));

    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt(""));
    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt("not-a-jwt"));
    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt("only.two"));
    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt("a.b.c.d"));
    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt("a..c"));
    EXPECT_FALSE(StorageInterceptUtils::LooksLikeJwt("-----BEGIN PRIVATE KEY-----"));
}