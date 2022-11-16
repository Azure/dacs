///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "MockRemoteClient.h"
#include "MockReadChunkApi.h"
#include "MockReadDirectoryApi.h"
#include "MockGetPropertiesApi.h"
//---------------------------------------------------------------------------------------------------------------------
MockRemoteClient::MockRemoteClient(const std::string& mountName)
    : m_strFsRoot(StorageInterceptConfig::GetInstance().GetMockClientRoot(mountName))
{
    LOG_INFO("Initialized mock remote of client name={} mockFsRoot={}", mountName, m_strFsRoot);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t MockRemoteClient::ReadChunkSync(
    const std::string& remotePath,
    void *buf,
    size_t length,
    off_t offset)
{
    MockReadChunkApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        buf,
        length,
        offset);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
std::string MockRemoteClient::ReadDirectory(
    const std::string& remotePath,
    const std::string& continuationToken,
    std::vector<DirectoryEntry>& dirEntries)
{
    MockReadDirectoryApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        continuationToken,
        dirEntries);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
int MockRemoteClient::GetProperties(
    const std::string& remotePath,
    FileAttributes& attributes)
{
    MockGetPropertiesApi api(
        StorageInterceptUtils::CombinePath(m_strFsRoot, remotePath),
        attributes);
    api.Execute();
    return api.GetResult();
}
//---------------------------------------------------------------------------------------------------------------------
