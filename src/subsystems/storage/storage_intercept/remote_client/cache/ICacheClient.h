//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "FileReadData.h"
#include <string>
#include <unistd.h>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class ICacheClient
{
    public:
        enum CacheStatus {SUCCESS, NOTFOUND, NOTFOUND_LOCKED, INTERNAL_ERROR};

        virtual CacheStatus DownloadSync(
            /*[in] */ const std::string& cacheKey,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char *buffer,
            /*[out]*/ uint64_t& readLength) = 0;

        virtual bool UploadAsync(
            /*[in] */ const std::string& cacheKey,
            /*[in] */ std::shared_ptr<const FileReadData> fileData) = 0;

        virtual ~ICacheClient() {};
};
//---------------------------------------------------------------------------------------------------------------------