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

// Network-free unit tests for SdkBlobClient's pure helpers: block-id encoding, CRC64
// checksum-mismatch classification, and container/blob path parsing across mount types.
//---------------------------------------------------------------------------------------------------------------------
#include "SdkBlobClient.h"
#include "TestBase.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"

#include <azure/core/base64.hpp>
#include "gtest/gtest.h"

#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
// Friend of SdkBlobClient (see the header). Friendship isn't inherited, so TEST_F bodies reach the
// privates through these accessors, which are members of this fixture.
class SdkBlobClientTest : public TestBase
{
    protected:
        static std::string CallGetUrlSafeBase64(const std::string& uuid, off64_t blockId)
        {
            return SdkBlobClient::GetUrlSafeBase64(uuid, blockId);
        }
        static bool CallIsChecksumMismatch(const std::string& message)
        {
            return SdkBlobClient::IsChecksumMismatch(message);
        }
        static std::string CallGetContainerName(const SdkBlobClient& client, const std::string& path)
        {
            return client.GetContainerName(path);
        }
        static std::string CallGetBlobName(const SdkBlobClient& client, const std::string& path)
        {
            return client.GetBlobName(path);
        }

        // Static-account mount with a fake token; SdkBlobClient construction is network-free.
        void ConfigureStaticAccount()
        {
            OverrideConfig(StorageInterceptConstants::ConfigNameRemoteType,
                StorageInterceptConstants::ConfigValRemoteTypeBlob);
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicAccount, "false");
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "false");
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobStorageAccName, "myacct");
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobStorageEndpointUrl,
                "https://myacct.blob.core.windows.net");
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobContainerName, "mycontainer");
            OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobAccessToken, "fake-access-token");
        }
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SdkBlobClientTest, GetUrlSafeBase64_IsFixedLengthDeterministicAndReversible)
{
    const std::string uuid = "12345678-1234-1234-1234-123456789012"; // 36 chars, like a real UUID

    const std::string id0 = CallGetUrlSafeBase64(uuid, 0);
    const std::string id1 = CallGetUrlSafeBase64(uuid, 1);
    const std::string id10 = CallGetUrlSafeBase64(uuid, 10);

    EXPECT_EQ(id0, CallGetUrlSafeBase64(uuid, 0));
    EXPECT_NE(id0, id1);
    EXPECT_NE(id1, id10);
    // Equal-length block ids across a blob: a 48-byte pre-image -> 64-char base64.
    EXPECT_EQ(id0.size(), id1.size());
    EXPECT_EQ(id1.size(), id10.size());

    // Pre-image is exactly 48 bytes: uuid + '-' padding + decimal block id.
    const std::vector<uint8_t> decoded = Azure::Core::Convert::Base64Decode(id0);
    const std::string decodedStr(decoded.begin(), decoded.end());
    EXPECT_EQ(decodedStr.size(), 48u);
    EXPECT_EQ(decodedStr.substr(0, uuid.size()), uuid);
    EXPECT_EQ(decodedStr.back(), '0');
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SdkBlobClientTest, IsChecksumMismatch_OnlyTrueForChecksumMessages)
{
    // Genuine CRC64 integrity failures from the structured-message decoder.
    EXPECT_TRUE(CallIsChecksumMismatch("Segment checksum mismatch at offset 0"));
    EXPECT_TRUE(CallIsChecksumMismatch("Stream checksum mismatch"));
    EXPECT_TRUE(CallIsChecksumMismatch("crc64 checksum mismatch detected"));

    // Every other error must NOT be mislabelled as corruption.
    EXPECT_FALSE(CallIsChecksumMismatch("The specified blob does not exist."));
    EXPECT_FALSE(CallIsChecksumMismatch("AuthenticationFailed"));
    EXPECT_FALSE(CallIsChecksumMismatch("The server is busy."));
    EXPECT_FALSE(CallIsChecksumMismatch(""));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SdkBlobClientTest, PathParsing_StaticAccount)
{
    ConfigureStaticAccount();
    SdkBlobClient client("");

    // Static account + static container: container comes from config, blob = full path.
    EXPECT_EQ(CallGetContainerName(client, "dir/sub/file.bin"), "mycontainer");
    EXPECT_EQ(CallGetBlobName(client, "dir/sub/file.bin"), "dir/sub/file.bin");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SdkBlobClientTest, PathParsing_DynamicContainer)
{
    ConfigureStaticAccount();
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "true");
    SdkBlobClient client("");

    // First path component is the container; the remainder is the blob.
    EXPECT_EQ(CallGetContainerName(client, "cont/dir/file.bin"), "cont");
    EXPECT_EQ(CallGetBlobName(client, "cont/dir/file.bin"), "dir/file.bin");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SdkBlobClientTest, PathParsing_DynamicAccount)
{
    OverrideConfig(StorageInterceptConstants::ConfigNameRemoteType,
        StorageInterceptConstants::ConfigValRemoteTypeBlob);
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicAccount, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobAccessToken, "fake-access-token");
    SdkBlobClient client("");

    // Path is <accountHost>/<container>/<blob...>: strip the host, then the first component is the
    // container and the rest is the blob.
    const std::string path = "myacct.blob.core.windows.net/cont/dir/file.bin";
    EXPECT_EQ(CallGetContainerName(client, path), "cont");
    EXPECT_EQ(CallGetBlobName(client, path), "dir/file.bin");
}
//---------------------------------------------------------------------------------------------------------------------
