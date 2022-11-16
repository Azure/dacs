//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "ICacheClient.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class LocalFSCacheClient : public ICacheClient
{
    public:
        LocalFSCacheClient();
        ~LocalFSCacheClient();

        CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& cacheKey,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char *buffer,
            /*[out]*/ uint64_t& readLength) override;

        bool UploadAsync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& cacheKey,
            /*[in] */ std::shared_ptr<const FileReadData> fileData) override;

    private:
        const std::string m_strLocalCachePath;
};
//---------------------------------------------------------------------------------------------------------------------
