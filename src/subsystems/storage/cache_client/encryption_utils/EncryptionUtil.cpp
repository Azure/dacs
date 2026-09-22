/// Copyright (c) Microsoft Corporation.
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

#include "EncryptionUtil.h"
#include "CommonLogger.h"
#include "CacheUtils.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <cstring>
#include <limits>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    constexpr int GCM_TAG_LEN = 16;
    constexpr int GCM_IV_LEN = 12;
}
//---------------------------------------------------------------------------------------------------------------------
EncryptionUtil::EncryptionUtil(bool encryptionEnabled, const char* encryptionKey)
: m_encryptionEnabled(encryptionEnabled)
{
    if (m_encryptionEnabled)
    {
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, encryptionKey, strlen(encryptionKey));
        SHA256_Final(m_encryptionKey, &sha256);
    }
}
//---------------------------------------------------------------------------------------------------------------------
EncryptionUtil::~EncryptionUtil()
{
}
//---------------------------------------------------------------------------------------------------------------------
bool EncryptionUtil::IsEncryptionEnabled() const
{
    return m_encryptionEnabled;
}
//---------------------------------------------------------------------------------------------------------------------
int EncryptionUtil::GcmEncrypt(unsigned char *plaintext, int plaintextLen,
                const std::string& iv,
                unsigned char *ciphertext,
                string& tag)
{
    if (plaintextLen <= 0)
    {
        LOG_ERROR("invalid buffer length: {}", plaintextLen);
        return -1;
    }

    // Create and initialise the context
    EVP_CIPHER_CTX *ctx;
    if(!(ctx = EVP_CIPHER_CTX_new()))
    {
        LOG_ERROR("EVP_CIPHER_CTX_new failed");
        return -1;
    }

    /* Initialise the encryption operation. */
    if(1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, m_encryptionKey, (const unsigned char*)iv.data()))
    {
        LOG_ERROR("EVP_EncryptInit_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = -1;
    /*
    * Provide the message to be encrypted, and obtain the encrypted output.
    * EVP_EncryptUpdate can be called multiple times if necessary
    */
    if(1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintextLen))
    {
        LOG_ERROR("EVP_EncryptUpdate failed");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int ciphertextLen = len;

    /*
    * Finalise the encryption. Normally ciphertext bytes may be written at
    * this stage, but this does not occur in GCM mode
    */
    if(1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len))
    {
        LOG_ERROR("EVP_EncryptFinal_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    ciphertextLen += len;

    // Get the tag
    if(1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LEN, (char*)tag.data()))
    {
        LOG_ERROR("Failed to get the tag for authentication");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    EVP_CIPHER_CTX_free(ctx);
    return ciphertextLen;
}
//---------------------------------------------------------------------------------------------------------------------
int EncryptionUtil::GcmDecrypt(unsigned char *ciphertext, int ciphertextLen,
                const std::string& tag,
                const std::string& iv,
                unsigned char *plaintext)
{
    if (ciphertextLen <= 0)
    {
        LOG_ERROR("invalid buffer length: {}", ciphertextLen);
        return -1;
    }

    // GCM_SET_TAG/DecryptInit_ex read a fixed number of bytes from tag/iv.data(). Reject
    // undersized values up front so a malicious/corrupt server response can't trigger an
    // out-of-bounds read of the std::string's backing buffer.
    if (tag.size() != GCM_TAG_LEN)
    {
        LOG_ERROR("invalid GCM tag length: {}", tag.size());
        return -1;
    }
    if (iv.size() != GCM_IV_LEN)
    {
        LOG_ERROR("invalid GCM iv length: {}", iv.size());
        return -1;
    }

    // Create and initialise the context
    EVP_CIPHER_CTX *ctx;
    if(!(ctx = EVP_CIPHER_CTX_new()))
    {
        LOG_ERROR("EVP_CIPHER_CTX_new failed");
        return -1;
    }

    // Initialise the decryption operation
    if(!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, m_encryptionKey, (const unsigned char*)iv.data()))
    {
        LOG_ERROR("EVP_DecryptInit_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    /*
    * Provide the message to be decrypted, and obtain the plaintext output.
    * EVP_DecryptUpdate can be called multiple times if necessary
    */
    int len;
    if(!EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertextLen))
    {
        LOG_WARN("EVP_DecryptUpdate failed");
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_cleanse(plaintext, static_cast<size_t>(ciphertextLen));
        return -1;
    }

    int plaintextLen = len;

    // Set expected tag value
    if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LEN, (unsigned char*)tag.data()))
    {
        LOG_ERROR("EVP_CIPHER_CTX_ctrl failed");
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_cleanse(plaintext, static_cast<size_t>(ciphertextLen));
        return -1;
    }

    /*
    * Finalise the decryption. A positive return value indicates success,
    * anything else is a failure - the plaintext is not trustworthy.
    */
    int ret = EVP_DecryptFinal_ex(ctx, plaintext + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret > 0)
    {
        // Success
        plaintextLen += len;
        return plaintextLen;
    }
    else
    {
        // EVP_DecryptUpdate may already have written unauthenticated plaintext, so cleanse
        // the complete output before returning the authentication failure.
        LOG_WARN("Encrypted message verification failed");
        OPENSSL_cleanse(plaintext, static_cast<size_t>(ciphertextLen));
        return -1;
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::string EncryptionUtil::GenerateIV()
{
    std::string iv(GCM_IV_LEN, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), GCM_IV_LEN) != 1)
    {
        LOG_ERROR("Failed to generate random GCM IV");
        return {};
    }
    return iv;
}
//---------------------------------------------------------------------------------------------------------------------
bool EncryptionUtil::DoEncrypt(UploadRequest* uploadRequest,
                               const char* inBuffer,
                               std::size_t inBufferLen,
                               std::unique_ptr<char[]>& encBuffer,
                               int64_t& encLen)
{
    if (!m_encryptionEnabled)
    {
        return true;
    }

    if (inBufferLen == 0 || inBufferLen > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        LOG_WARN_REQID("Invalid encryption input length: {}", *uploadRequest, inBufferLen);
        return false;
    }

    const int inputLength = static_cast<int>(inBufferLen);

    std::string iv = GenerateIV();
    if (iv.size() != GCM_IV_LEN)
    {
        return false;
    }
    std::string tag(GCM_TAG_LEN, '\0');

    std::unique_ptr<char[]> buffer(new char[inBufferLen]);
    encLen = GcmEncrypt((unsigned char*)inBuffer, inputLength, iv, (unsigned char*)buffer.get(), tag);
    if(encLen != inputLength)
    {
        // We use GCM mode which doesn't require any padding, so length of message should not be affected 
        LOG_WARN_REQID("Encryption failed", *uploadRequest);
        return false;
    }

    encBuffer.reset(buffer.release());

    auto& fileMetadata = *(uploadRequest->mutable_metadata());
    fileMetadata[CACHE_META_ENCRYPTION_ENABLED] = "true";
    fileMetadata[CACHE_META_ENCRYPTION_ORIG_LEN] = std::to_string(inBufferLen);
    fileMetadata[CACHE_META_ENCRYPTION_IV] = iv;
    fileMetadata[CACHE_META_ENCRYPTION_TAG] = tag;

    LOG_DEBUG_REQID("Encrypted input buffer. len: {}", *uploadRequest, inBufferLen);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool EncryptionUtil::DoDecrypt(const DownloadResponse& response, IConnection* serverConnection, char* buffer,
    uint64_t& readLength)
{
    if (!m_encryptionEnabled)
    {
        return cacheutils::ReadAll(serverConnection, buffer, readLength);
    }

    // fetch encryption params from response
    auto& fileMetadata = response.metadata();
    auto strEncFlag = fileMetadata.find(CACHE_META_ENCRYPTION_ENABLED);
    if (strEncFlag == fileMetadata.end() || strEncFlag->second != "true")
    {
        LOG_WARN("Encryption flag not set for the cache key");
        return false;
    }

    auto strLen = fileMetadata.find(CACHE_META_ENCRYPTION_ORIG_LEN);
    if (strLen == fileMetadata.end())
    {
        LOG_WARN("Encryption length not set for the cache key");
        return false;
    }

    const uint64_t parsedOrigLen = std::strtoull(strLen->second.c_str(), nullptr, 0);
    if (parsedOrigLen == 0 || parsedOrigLen > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    {
        LOG_WARN("Invalid encrypted length: {}", parsedOrigLen);
        return false;
    }
    const int origLen = static_cast<int>(parsedOrigLen);
    if (readLength < static_cast<uint64_t>(origLen))
    {
        LOG_WARN("cannot read encrypted chunk partially. requested: {}, actual: {}", readLength, origLen);
        return false;
    }

    auto iv = fileMetadata.find(CACHE_META_ENCRYPTION_IV);
    if (iv == fileMetadata.end())
    {
        LOG_WARN("Encryption iv not set for the cache key");
        return false;
    }

    auto tag = fileMetadata.find(CACHE_META_ENCRYPTION_TAG);
    if (tag == fileMetadata.end())
    {
        LOG_WARN("Encryption tag not set for the cache key");
        return false;
    }

    std::unique_ptr<char[]> encBuffer(new char[static_cast<size_t>(origLen)]);
    bool bRet = cacheutils::ReadAll(serverConnection, encBuffer.get(), static_cast<uint64_t>(origLen));
    if (!bRet)
    {
        LOG_WARN("Failed to read complete message from server, id: {}", serverConnection->GetId());
        return false;
    }

    int plainLen = GcmDecrypt((unsigned char*)encBuffer.get(), origLen, tag->second, iv->second,
        (unsigned char*)buffer);
    if (plainLen <= 0)
    {
        LOG_WARN("Decryption failed");
        return false;
    }

    if (plainLen != origLen)
    {
        LOG_WARN("Decrypted len does not match original. orig: {}, dec: {}", origLen, plainLen);
        OPENSSL_cleanse(buffer, static_cast<size_t>(origLen));
        return false;
    }

    LOG_DEBUG("Decrypted buffer - original len: {}, enc len: {}", origLen, readLength);
    readLength = static_cast<uint64_t>(plainLen);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------