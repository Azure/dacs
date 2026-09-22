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

#pragma once
#include "cache.pb.h"
#include "IConnection.h"
#include <openssl/sha.h>
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
class EncryptionUtilTest;
class EncryptionUtil
{
    // Friends for testing
    FRIEND_TEST(EncryptionUtilTest, EncryptDecryptTest);
    FRIEND_TEST(EncryptionUtilTest, DecryptFailureDoesNotLeakPlaintextTest);
    FRIEND_TEST(EncryptionUtilTest, DoDecryptFailureCleansesCallerBufferTest);
    FRIEND_TEST(EncryptionUtilTest, DecryptRejectsUndersizedTagAndIvTest);
    FRIEND_TEST(EncryptionUtilTest, RejectsLengthsLargerThanOpenSslIntTest);

    public:
        EncryptionUtil(bool encryptionEnabled, const char* encryptionKey);
        ~EncryptionUtil();

    public:
        bool IsEncryptionEnabled() const;

        /*
        * Check if encrytion is enabled and create a new buffer with encrypted data.
        * encLen will not be updated if encryption is disabled and client should continue to use the original buffer.
        */
        bool DoEncrypt(
            /*[out]*/ cache::UploadRequest* uploadRequest,
            /*[in] */ const char* inBuffer,
            /*[in] */ std::size_t inBufferLen,
            /*[out]*/ std::unique_ptr<char[]>& encBuffer,
            /*[out]*/ int64_t& encLen);

        /*
        * Check if encrytion is enabled and create a new buffer with decrypted data.
        * If encryption is disabled the remote data will be directly copied into the supplied buffer.
        */
        bool DoDecrypt(
            /*[in ]*/ const cache::DownloadResponse& response,
            /*[in ]*/ IConnection* serverConnection,
            /*[out]*/ char* buffer,
            /*[out]*/ uint64_t& readLength);

    protected:
        int GcmEncrypt(
            /*[in ]*/ unsigned char *plaintext, 
            /*[in ]*/ int plaintextLen,
            /*[in ]*/ const std::string& iv,
            /*[out]*/ unsigned char *ciphertext,
            /*[out]*/ std::string& tag);

        int GcmDecrypt(
            /*[in ]*/ unsigned char *ciphertext,
            /*[in ]*/ int ciphertextLen,
            /*[in ]*/ const std::string& tag,
            /*[in ]*/ const std::string& iv,
            /*[out]*/ unsigned char *plaintext);

        std::string GenerateIV();

    private:
        constexpr static const char* const CACHE_META_ENCRYPTION_ENABLED = "ENC_FLAG";
        constexpr static const char* const CACHE_META_ENCRYPTION_ORIG_LEN = "ENC_ORIG_LEN";
        constexpr static const char* const CACHE_META_ENCRYPTION_IV = "ENC_IV";
        constexpr static const char* const CACHE_META_ENCRYPTION_TAG = "ENC_TAG";

        const bool m_encryptionEnabled;
        unsigned char m_encryptionKey[SHA256_DIGEST_LENGTH];
};
//---------------------------------------------------------------------------------------------------------------------