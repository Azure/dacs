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
//---------------------------------------------------------------------------------------------------------------------
// IRemoteClient backed by the official azure-sdk-for-cpp (azure-storage-blobs), the migration target
// replacing the legacy cpplite BlobClient.
//
// Differences from cpplite:
// Connection management differs from cpplite: cpplite sized a per-client connection pool from
// maxConnsPerBlobClient that both reused keep-alive connections and capped concurrency. The SDK has
// no per-client pool - connections are reused from a global idle cache and created on demand - so
// maxConnsPerBlobClient is not consumed here directly. Concurrency is instead bounded one layer up in
// StreamingClient via chunkOperationMaxConcurrency (which defaults to maxConnsPerBlobClient); since
// each chunk op issues one request (one connection), the chunk hot path stays capped as before.
//---------------------------------------------------------------------------------------------------------------------
#include "IRemoteClient.h"

#include <azure/storage/blobs.hpp>
#include <azure/core/credentials/credentials.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class SdkBlobClient : public IRemoteClient
{
    // Test-only access to the private path/block-id/checksum helpers.
    friend class SdkBlobClientTest;

    typedef Azure::Storage::Blobs::BlobServiceClient   azBlobServiceClient_t;
    typedef Azure::Storage::Blobs::BlobContainerClient azBlobContainerClient_t;
    typedef Azure::Storage::Blobs::BlobClientOptions   azBlobClientOptions_t;

    public:
        explicit SdkBlobClient(const std::string& mountName);

        ssize_t ReadChunkSync(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void* buf,
            /*[in] */ size_t length,
            /*[in] */ off_t offset) override;

        std::string ReadDirectory(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries) override;

        int GetProperties(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ FileAttributes& attr) override;

        void PrepareBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ size_t numBlocks) override;

        int StageBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const void* buf,
            /*[in]*/ size_t length,
            /*[in]*/ off64_t blockId) override;

        int CommitBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const std::string& version) override;

    private:
        enum class AuthKind { SharedKey, Sas, Token };

        struct SdkBlobEntry
        {
            std::string name;
            bool        isDirectory = false;
            int64_t     contentLength = 0;
            time_t      lastModified = 0;
            std::string version;
        };

        void ResolveAuth();
        bool DetectTokenFetchEnabled() const;

        std::shared_ptr<azBlobServiceClient_t> GetServiceClient(const std::string& remotePath);
        std::shared_ptr<azBlobServiceClient_t> BuildServiceClient(const std::string& endpointUrl);
        azBlobContainerClient_t GetContainerClient(const std::string& remotePath);

        std::string GetContainerName(const std::string& remotePath) const;
        std::string GetBlobName(const std::string& remotePath) const;
        static std::string GetUrlSafeBase64(const std::string& uuid, off64_t blockId);

        // True only for a CRC64 "checksum mismatch" error (not auth/404/throttle/network).
        static bool IsChecksumMismatch(const std::string& message);

        // Returns whether a failed download should be retried: false if it is not a CRC64 mismatch
        // (caller handles it generically); for a mismatch, backs off and returns true while retries
        // remain, or (exhausted) logs the integrity error and throws. `detail` is the exception message.
        bool ShouldRetryOnChecksumMismatch(const std::string& remotePath, off_t offset, size_t length,
                                    uint32_t attempt, uint32_t maxAttempts, const std::string& detail);

        std::vector<SdkBlobEntry> ListOneBlob(const std::string& remotePath, const std::string& queryPrefix);
        static void MakeDirectoryAttr(FileAttributes& attr);
        static void MakeFileAttr(const SdkBlobEntry& entry, FileAttributes& attr);

        azBlobClientOptions_t MakeClientOptions() const;

    private:
        const std::string m_strMountName;
        const bool        m_bIsDynamicAccount;
        const bool        m_bIsDynamicContainer;
        const bool        m_bChecksumEnabled;
        const uint32_t    m_nChecksumRetryMaxAttempts;
        const std::chrono::seconds m_blobRetryBaseDelay;
        const std::chrono::seconds m_blobRetryMaxDelay;
        bool              m_isTokenFetchEnabled = false;

        // Auth
        AuthKind          m_authKind = AuthKind::Token;
        std::string       m_accountName;
        std::string       m_accountKey;
        std::string       m_sasToken;
        std::string       m_staticAccessToken;
        std::string       m_endpointUrl;    // static account only
        std::string       m_strContainerName;
        std::shared_ptr<Azure::Core::Credentials::TokenCredential> m_tokenCredential;

        // Per-account service clients ("" key for static account)
        std::map<std::string, std::shared_ptr<azBlobServiceClient_t>> m_mapServiceClients;
        std::mutex m_lockServiceClients;

        // Block upload state, keyed by remote path
        std::map<std::string, std::vector<std::string>> m_mapBlockList;
        std::mutex m_lockBlockList;
};
//---------------------------------------------------------------------------------------------------------------------
