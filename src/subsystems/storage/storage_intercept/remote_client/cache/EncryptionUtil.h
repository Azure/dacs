//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "cache.pb.h"
#include "FileReadData.h"
#include <openssl/sha.h>
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
class EncryptionUtilTest;
class EncryptionUtil
{
    // Friends for testing
    FRIEND_TEST(EncryptionUtilTest, EncryptDecryptTest);

    public:
        EncryptionUtil(bool encryptionEnabled, const std::string& encryptionKey);
        ~EncryptionUtil();

    public:
        /*
        * Check if encrytion is enabled and create a new buffer with encrypted data.
        * encLen will not be updated if encryption is disabled and client should continue to use the original buffer.
        */
        bool DoEncrypt(
            /*[out]*/ cache::UploadRequest* uploadRequest,
            /*[in] */ std::shared_ptr<const FileReadData>& m_fileData,
            /*[out]*/ std::unique_ptr<char[]>& encBuffer,
            /*[out]*/ int64_t& encLen);

        /*
        * Check if encrytion is enabled and create a new buffer with decrypted data.
        * If encryption is disabled the remote data will be directly copied into the supplied buffer.
        */
        bool DoDecrypt(
            /*[in ]*/ const cache::DownloadResponse& response,
            /*[in ]*/ int serverFd,
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
        const char* const CACHE_META_ENCRYPTION_ENABLED = "ENC_FLAG";
        const char* const CACHE_META_ENCRYPTION_ORIG_LEN = "ENC_ORIG_LEN";
        const char* const CACHE_META_ENCRYPTION_IV = "ENC_IV";
        const char* const CACHE_META_ENCRYPTION_TAG = "ENC_TAG";

        const bool m_encryptionEnabled;
        unsigned char m_encryptionKey[SHA256_DIGEST_LENGTH];
};
//---------------------------------------------------------------------------------------------------------------------