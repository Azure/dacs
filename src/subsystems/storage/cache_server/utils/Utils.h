///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "cache.pb.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
enum CacheStatus
{
    NOTFOUND, INITIATED, STARTED, COMPLETED
};
//---------------------------------------------------------------------------------------------------------------------
struct CacheInfo
{
    CacheInfo(uint64_t fileSize, CacheStatus status) : m_fileSize(fileSize), m_status(status) {}
    CacheInfo() : m_fileSize(0), m_status(NOTFOUND) {}

    uint64_t m_fileSize;
    CacheStatus m_status;
};
//---------------------------------------------------------------------------------------------------------------------
struct CacheAttributes
{
    CacheAttributes(bool isdir, int64_t filesize, uint64_t accessedtime, uint64_t modifiedtime)
        : m_isdir(isdir), m_filesize(filesize), m_accessedtime(accessedtime), m_modifiedtime(modifiedtime) {}
    CacheAttributes() 
        : m_filesize(-1) {}

    bool m_isdir;
    int64_t m_filesize;
    uint64_t m_accessedtime;
    uint64_t m_modifiedtime;
};
//---------------------------------------------------------------------------------------------------------------------
void SetCacheRootPath(const std::string& rootFolder);
void SetAttributeCacheLimit(const uint64_t limitCacheEntries);
std::string GetCacheFolderPath();
std::string GetCacheTempFolderPath();
std::string GetCacheTempPath(const std::string& fileName);
std::string GetCachePath(const std::string& fileName);
void Initialize();
bool GetLockForUpload(const std::string& fileName);
CacheInfo GetAndInitCacheStatus(const std::string& fileName);
CacheInfo GetCacheStatus(const std::string& fileName);
bool UpdateCacheStatus(const std::string& fileName, bool ignoreLock, CacheInfo cacheInfo);
bool GetAndInitAttribute(const std::string& fileName, CacheAttributes& fileAttributes);
bool GetAttribute(const std::string& fileName, CacheAttributes& fileAttributes);
bool PutAttributes(const cache::PutAttributeRequest& putAttributeRequest);
//---------------------------------------------------------------------------------------------------------------------