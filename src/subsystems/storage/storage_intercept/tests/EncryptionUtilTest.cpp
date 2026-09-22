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

#include "EncryptionUtilTest.h"
#include "StorageInterceptLogger.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    class EncryptionTestConnection final : public IConnection
    {
        public:
            explicit EncryptionTestConnection(vector<char> data)
                : m_data(std::move(data))
            {
            }

            int Send(const char*, uint64_t size) override
            {
                return static_cast<int>(size);
            }

            int SendAndFinalize(const char*, uint64_t size) override
            {
                return static_cast<int>(size);
            }

            int Receive(char* buffer, uint64_t size) override
            {
                const uint64_t remaining = m_data.size() - m_offset;
                const uint64_t bytesToCopy = std::min(size, remaining);
                if (bytesToCopy == 0)
                {
                    return 0;
                }

                std::memcpy(buffer, m_data.data() + m_offset, bytesToCopy);
                m_offset += bytesToCopy;
                return static_cast<int>(bytesToCopy);
            }

            int GetId() override
            {
                return 1;
            }

            bool IsValidConnection() override
            {
                return true;
            }

            void ResetConnection() override
            {
                m_offset = 0;
            }

        private:
            vector<char> m_data;
            uint64_t m_offset = 0;
    };
}
//---------------------------------------------------------------------------------------------------------------------
EncryptionUtilTest::EncryptionUtilTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void EncryptionUtilTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
void EncryptionUtilTest::GetRandDataBuffer(unsigned char* buffer, size_t bufSize)
{
    std::generate(buffer, buffer + bufSize, []{return rand() % 255;});
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(EncryptionUtilTest, EncryptDecryptTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey.c_str());

    // 12 bytes IV
    string iv = encUtil.GenerateIV();
    ASSERT_EQ(static_cast<size_t>(12), iv.size());

    // mac tag should be 16 bytes
    string tag(16, '\0');

    unsigned char origBuffer[100];
    GetRandDataBuffer(origBuffer, 100);

    unsigned char encBuffer[100+16];
    int encLen = encUtil.GcmEncrypt(origBuffer, 100, iv, encBuffer, tag);
    ASSERT_EQ(100, encLen);

    unsigned char decBuffer[100];
    int finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(100, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try minimum size 1
    GetRandDataBuffer(origBuffer, 1);
    encLen = encUtil.GcmEncrypt(origBuffer, 1, iv, encBuffer, tag);
    ASSERT_EQ(1, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // pass invalid tag
    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, "1234567890123456", iv, decBuffer);
    ASSERT_EQ(-1, finalLen);

    // pass invalid iv
    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, "123456789011", decBuffer);
    ASSERT_EQ(-1, finalLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try invalid key
    string invalid_key = "1234567891";
    EncryptionUtil encUtilBad(true, invalid_key.c_str());
    finalLen = encUtilBad.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(-1, finalLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(1, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // zero length not supported
    encLen = encUtil.GcmEncrypt(origBuffer, 0, iv, encBuffer, tag);
    ASSERT_EQ(-1, encLen);

    // try different lengths to check block size boundaries
    GetRandDataBuffer(origBuffer, 16);
    encLen = encUtil.GcmEncrypt(origBuffer, 16, iv, encBuffer, tag);
    ASSERT_EQ(16, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(16, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 32);
    encLen = encUtil.GcmEncrypt(origBuffer, 32, iv, encBuffer, tag);
    ASSERT_EQ(32, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(32, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 15);
    encLen = encUtil.GcmEncrypt(origBuffer, 15, iv, encBuffer, tag);
    ASSERT_EQ(15, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(15, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 17);
    encLen = encUtil.GcmEncrypt(origBuffer, 17, iv, encBuffer, tag);
    ASSERT_EQ(17, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(17, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 31);
    encLen = encUtil.GcmEncrypt(origBuffer, 31, iv, encBuffer, tag);
    ASSERT_EQ(31, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(31, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    GetRandDataBuffer(origBuffer, 33);
    encLen = encUtil.GcmEncrypt(origBuffer, 33, iv, encBuffer, tag);
    ASSERT_EQ(33, encLen);

    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(33, finalLen);
    AssertEqArray<unsigned char>(origBuffer, decBuffer, finalLen);

    // try large buffer (to match cache chunk size)
    unique_ptr<unsigned char[]>largeOrigBuffer(new unsigned char[4*1024*1024 + 1]);
    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024);

    unique_ptr<unsigned char[]> largeEncBuffer(new unsigned char[4*1024*1024 + 1 + 16]);
    unique_ptr<unsigned char[]> largeDecBuffer(new unsigned char[4*1024*1024 + 1]);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);

    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024 + 1);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024 + 1, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024 + 1);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024 + 1, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);

    GetRandDataBuffer(largeOrigBuffer.get(), 4*1024*1024 - 1);
    encLen = encUtil.GcmEncrypt(largeOrigBuffer.get(), 4*1024*1024 - 1, iv, largeEncBuffer.get(), tag);
    ASSERT_EQ(encLen, 4*1024*1024 - 1);

    finalLen = encUtil.GcmDecrypt(largeEncBuffer.get(), encLen, tag, iv, largeDecBuffer.get());
    ASSERT_EQ(4*1024*1024 - 1, finalLen);
    AssertEqArray<unsigned char>(largeOrigBuffer.get(), largeDecBuffer.get(), finalLen);
}
//---------------------------------------------------------------------------------------------------------------------
// Regression test: GcmDecrypt must never leave unauthenticated plaintext behind in its output
// buffer when tag verification fails. Prior to the fix, EVP_DecryptUpdate wrote plaintext into
// the output buffer before EVP_DecryptFinal_ex verified the GCM tag, so a failed verification
// still left recoverable plaintext bytes in the buffer.
TEST_F(EncryptionUtilTest, DecryptFailureDoesNotLeakPlaintextTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey.c_str());

    string iv = encUtil.GenerateIV();
    string tag(16, '\0');

    unsigned char origBuffer[100];
    GetRandDataBuffer(origBuffer, 100);

    unsigned char encBuffer[100 + 16];
    int encLen = encUtil.GcmEncrypt(origBuffer, 100, iv, encBuffer, tag);
    ASSERT_EQ(100, encLen);

    const unsigned char sentinel = 0xCC;
    unsigned char decBuffer[100];

    // Tamper with the tag so verification fails after EVP_DecryptUpdate has already run.
    string badTag = tag;
    badTag[0] ^= 0xFF;

    memset(decBuffer, sentinel, sizeof(decBuffer));
    int finalLen = encUtil.GcmDecrypt(encBuffer, encLen, badTag, iv, decBuffer);
    ASSERT_EQ(-1, finalLen);
    // The buffer must be cleansed (zeroed), not left holding the sentinel pattern (untouched)
    // nor the unauthenticated plaintext (leaked).
    for (int i = 0; i < 100; i++)
    {
        ASSERT_EQ(0, decBuffer[i]) << "unauthenticated plaintext byte " << i << " was not cleansed";
    }

    // Tamper with the ciphertext itself so the recovered "plaintext" differs from the
    // original but verification still fails - same requirement applies.
    unsigned char badEncBuffer[100 + 16];
    memcpy(badEncBuffer, encBuffer, static_cast<size_t>(encLen));
    badEncBuffer[0] ^= 0xFF;

    memset(decBuffer, sentinel, sizeof(decBuffer));
    finalLen = encUtil.GcmDecrypt(badEncBuffer, encLen, tag, iv, decBuffer);
    ASSERT_EQ(-1, finalLen);
    for (int i = 0; i < 100; i++)
    {
        ASSERT_EQ(0, decBuffer[i]) << "unauthenticated plaintext byte " << i << " was not cleansed";
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(EncryptionUtilTest, DoDecryptFailureCleansesCallerBufferTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey.c_str());

    string iv = encUtil.GenerateIV();
    string tag(16, '\0');

    unsigned char origBuffer[100];
    GetRandDataBuffer(origBuffer, sizeof(origBuffer));

    unsigned char encBuffer[100];
    int encLen = encUtil.GcmEncrypt(
        origBuffer, static_cast<int>(sizeof(origBuffer)), iv, encBuffer, tag);
    ASSERT_EQ(static_cast<int>(sizeof(origBuffer)), encLen);

    tag[0] ^= 0xFF;

    cache::DownloadResponse response;
    auto& metadata = *response.mutable_metadata();
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_ENABLED] = "true";
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_ORIG_LEN] = std::to_string(encLen);
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_IV] = iv;
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_TAG] = tag;

    vector<char> encryptedData(encBuffer, encBuffer + encLen);
    EncryptionTestConnection connection(std::move(encryptedData));

    constexpr char sentinel = static_cast<char>(0xCC);
    char callerBuffer[100];
    std::memset(callerBuffer, sentinel, sizeof(callerBuffer));
    uint64_t readLength = sizeof(callerBuffer);

    ASSERT_FALSE(encUtil.DoDecrypt(response, &connection, callerBuffer, readLength));
    ASSERT_TRUE(std::all_of(std::begin(callerBuffer), std::end(callerBuffer),
        [](char value) { return value == 0; }));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(EncryptionUtilTest, RejectsLengthsLargerThanOpenSslIntTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey.c_str());
    const size_t oversizedLength = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;

    cache::UploadRequest uploadRequest;
    char inputBuffer = 0;
    unique_ptr<char[]> encryptedBuffer;
    int64_t encryptedLength = 0;
    ASSERT_FALSE(encUtil.DoEncrypt(
        &uploadRequest, &inputBuffer, oversizedLength, encryptedBuffer, encryptedLength));

    cache::DownloadResponse downloadResponse;
    auto& metadata = *downloadResponse.mutable_metadata();
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_ENABLED] = "true";
    metadata[EncryptionUtil::CACHE_META_ENCRYPTION_ORIG_LEN] = std::to_string(oversizedLength);

    EncryptionTestConnection connection(vector<char>{});
    char outputBuffer = 0;
    uint64_t readLength = oversizedLength;
    ASSERT_FALSE(encUtil.DoDecrypt(downloadResponse, &connection, &outputBuffer, readLength));
}
//---------------------------------------------------------------------------------------------------------------------
// Regression test: reject undersized tag/iv values up front rather than reading past the end
// of their backing buffers.
TEST_F(EncryptionUtilTest, DecryptRejectsUndersizedTagAndIvTest)
{
    string encKey = "1234567890";
    EncryptionUtil encUtil(true, encKey.c_str());

    string iv = encUtil.GenerateIV();
    string tag(16, '\0');

    unsigned char origBuffer[100];
    GetRandDataBuffer(origBuffer, 100);

    unsigned char encBuffer[100 + 16];
    int encLen = encUtil.GcmEncrypt(origBuffer, 100, iv, encBuffer, tag);
    ASSERT_EQ(100, encLen);

    unsigned char decBuffer[100];

    // Undersized tag (e.g. a truncated/corrupt metadata value from a malicious server).
    string shortTag = tag.substr(0, 4);
    int finalLen = encUtil.GcmDecrypt(encBuffer, encLen, shortTag, iv, decBuffer);
    ASSERT_EQ(-1, finalLen);

    // Undersized iv.
    string shortIv = iv.substr(0, 4);
    finalLen = encUtil.GcmDecrypt(encBuffer, encLen, tag, shortIv, decBuffer);
    ASSERT_EQ(-1, finalLen);
}
//---------------------------------------------------------------------------------------------------------------------
