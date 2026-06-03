//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "FileData.h"
#include "CacheUtils.h"
#include <string>
#include <unistd.h>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class ICacheClient
{
    public:
        virtual cacheutils::CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char *buffer,
            /*[out]*/ uint64_t& readLength) = 0;

        virtual bool Upload(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ std::shared_ptr<const FileData> fileData,
            /*[in] */ bool isWrite) = 0;

        virtual bool DeleteGroup(const std::string& groupId) { return true; }

        virtual ~ICacheClient() {};
};
//---------------------------------------------------------------------------------------------------------------------