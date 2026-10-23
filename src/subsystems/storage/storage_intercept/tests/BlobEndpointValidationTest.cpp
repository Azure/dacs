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

#include "BlobClient.h"
#include "SdkBlobClient.h"
#include "RemoteClientException.h"
#include "StorageInterceptConstants.h"
#include "TestBase.h"

#include <cstdlib>
#include <memory>
#include <string>

// Client construction and lookup do not issue HTTP requests.
class BlobEndpointValidationTest : public TestBase, public ::testing::WithParamInterface<bool>
{
protected:
    void SetUp() override
    {
        const char* suffix = std::getenv("AZURE_BLOB_ENDPOINT_SUFFIX");
        m_hadSuffix = suffix != nullptr;
        m_savedSuffix = suffix ? suffix : "";
        ASSERT_EQ(unsetenv("AZURE_BLOB_ENDPOINT_SUFFIX"), 0);
        OverrideConfig(StorageInterceptConstants::ConfigNameRemoteType, StorageInterceptConstants::ConfigValRemoteTypeBlob);
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicAccount, "true");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobAccessToken, "fake-access-token");
        if (GetParam())
        {
            m_sdk = std::make_unique<SdkBlobClient>("");
        }
        else
        {
            m_legacy = std::make_unique<BlobClient>("");
        }
    }

    void TearDown() override
    {
        m_sdk.reset();
        m_legacy.reset();
        if (m_hadSuffix)
        {
            EXPECT_EQ(setenv("AZURE_BLOB_ENDPOINT_SUFFIX", m_savedSuffix.c_str(), 1), 0);
        }
        else
        {
            EXPECT_EQ(unsetenv("AZURE_BLOB_ENDPOINT_SUFFIX"), 0);
        }
        TestBase::TearDown();
    }

    bool Lookup(const std::string& host)
    {
        const std::string path = host + "/container/blob";
        if (m_sdk)
        {
            return m_sdk->GetServiceClient(path) != nullptr;
        }
        return m_legacy->GetBlobClient(path) != nullptr;
    }

private:
    bool m_hadSuffix = false;
    std::string m_savedSuffix;
    std::unique_ptr<SdkBlobClient> m_sdk;
    std::unique_ptr<BlobClient> m_legacy;
};

TEST_P(BlobEndpointValidationTest, AcceptsCloudEndpoints)
{
    for (const auto* host : {
        "account.blob.core.windows.net",
        "ACCOUNT.BLOB.CORE.WINDOWS.NET",
        "account.blob.core.chinacloudapi.cn",
        "account.blob.core.usgovcloudapi.net",
        "account.blob.core.cloudapi.de",
        "account.blob.core.microsoft.scloud",
        "account.blob.core.eaglex.ic.gov"})
    {
        SCOPED_TRACE(host);
        EXPECT_TRUE(Lookup(host));
        EXPECT_TRUE(Lookup(host));
    }
}

TEST_P(BlobEndpointValidationTest, RejectsUnknownAndMalformedHosts)
{
    for (const auto* host : {
        "unknown.example",
        "account.blob.core.windows.net.evil.example",
        "accountblob.core.windows.net",
        "user@account.blob.core.windows.net",
        "account.blob.core.windows.net:443",
        "account%2e.blob.core.windows.net",
        "account\\evil.blob.core.windows.net",
        "account\n.blob.core.windows.net",
        "account..blob.core.windows.net",
        ".account.blob.core.windows.net",
        ".blob.core.windows.net",
        "blob.core.windows.net",
        "account.blob.core.windows.net."})
    {
        SCOPED_TRACE(host);
        EXPECT_THROW(Lookup(host), RemoteClientException);
    }
}

TEST_P(BlobEndpointValidationTest, ChecksConfiguredSuffixEvenForCachedClients)
{
    for (const auto* suffix : {"blob.example.test", ".blob.example.test", ".BLOB.EXAMPLE.TEST"})
    {
        SCOPED_TRACE(suffix);
        ASSERT_EQ(setenv("AZURE_BLOB_ENDPOINT_SUFFIX", suffix, 1), 0);
        EXPECT_TRUE(Lookup("account.blob.example.test"));
        EXPECT_TRUE(Lookup("account.blob.core.windows.net"));
        EXPECT_THROW(Lookup("account.blob.example.test.evil.example"), RemoteClientException);
        EXPECT_THROW(Lookup("account..blob.example.test"), RemoteClientException);
        EXPECT_THROW(Lookup(".account.blob.example.test"), RemoteClientException);
        EXPECT_THROW(Lookup(".blob.example.test"), RemoteClientException);
        ASSERT_EQ(unsetenv("AZURE_BLOB_ENDPOINT_SUFFIX"), 0);
        EXPECT_THROW(Lookup("account.blob.example.test"), RemoteClientException);
    }
    ASSERT_EQ(setenv("AZURE_BLOB_ENDPOINT_SUFFIX", "blob..example.test", 1), 0);
    EXPECT_THROW(Lookup("account.blob..example.test"), RemoteClientException);
}

INSTANTIATE_TEST_SUITE_P(
    NativeBackends, BlobEndpointValidationTest, ::testing::Bool(),
    [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Sdk" : "Legacy"; });
