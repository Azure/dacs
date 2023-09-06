///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "EncryptionUtil.h"
#include "CommonLogger.h"
#include "CacheUtils.h"
#include <openssl/evp.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
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
    if(1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, (char*)tag.data()))
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
        return -1;
    }

    int plaintextLen = len;

    // Set expected tag value
    if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (unsigned char*)tag.data()))
    {
        LOG_ERROR("EVP_CIPHER_CTX_ctrl failed");
        EVP_CIPHER_CTX_free(ctx);
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
        // Verify failed
        LOG_WARN("Encrypted message verification failed");
        return -1;
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::string EncryptionUtil::GenerateIV()
{
    // Generate random IV of 12 bytes (default length)
    static thread_local boost::uuids::random_generator uuidGenerator;
    boost::uuids::uuid uuid = uuidGenerator();
    std::string uuidStr(uuid.begin(), uuid.end());
    return uuidStr.substr(0, 12);
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

    std::string iv = GenerateIV();
    std::string tag(16, '\0');

    std::unique_ptr<char[]> buffer(new char[inBufferLen]);
    encLen = GcmEncrypt((unsigned char*)inBuffer, inBufferLen, iv, (unsigned char*)buffer.get(), tag);
    if(encLen != inBufferLen)
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

    size_t origLen = std::strtoull(strLen->second.c_str(), nullptr, 0);
    if (readLength < origLen)
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

    std::unique_ptr<char[]> encBuffer(new char[origLen]);
    bool bRet = cacheutils::ReadAll(serverConnection, encBuffer.get(), origLen);
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
        return false;
    }

    LOG_DEBUG("Decrypted buffer - original len: {}, enc len: {}", origLen, readLength);
    readLength = plainLen;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------