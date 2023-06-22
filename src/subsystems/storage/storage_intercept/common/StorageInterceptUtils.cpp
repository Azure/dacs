//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtils.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include <cstring>
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptUtils::CombinePath(
    const std::string& basePath,
    const std::string& relativePath)
{
    std::string result = basePath;
    if (basePath.back() == SEPARATOR_CHAR)
    {
        result.pop_back();
    }

    if (relativePath.empty() || relativePath[0] == SEPARATOR_CHAR)
    {
        result += relativePath;
    }
    else
    {
        result += SEPARATOR_CHAR;
        result += relativePath;
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::GetRelativePath(
    const std::string& basePath,
    const char* path,
    std::string& outRelativePath)
{
    outRelativePath = "";

    int basePathIdx = 0;
    int pathIdx = 0;
    while (basePathIdx < basePath.size()) {
        if (path[pathIdx] == '\0') return false;

        if (basePath[basePathIdx] != path[pathIdx]) return false;

        basePathIdx++;

        while (path[pathIdx] == SEPARATOR_CHAR && path[pathIdx+1] == SEPARATOR_CHAR) pathIdx++;
        pathIdx++;
    }

    if (path[pathIdx] == '\0') {
        // basePath and path point to same directory.
        outRelativePath = "";
        return true;
    }

    if (path[pathIdx] != SEPARATOR_CHAR) {
        // basePath is substring of path but not its parent. Example basePath=/foo path=/foot/bar
        return false;
    }

    // Don't include starting separator in relative path
    while (path[pathIdx] == SEPARATOR_CHAR) pathIdx++;

    // Append rest of path to relative path (ignoring consecutive separators)
    while (path[pathIdx] != '\0') {
        outRelativePath += path[pathIdx];

        while (path[pathIdx] == SEPARATOR_CHAR && path[pathIdx+1] == SEPARATOR_CHAR) pathIdx++;
        pathIdx++;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptUtils::GetFirstComponentFromPath(const std::string& path)
{
    if (path == "")
    {
        LOG_ERROR("GetFirstComponentFromPath : Invalid path provided. path={}", path);
        throw "GetFirstComponentFromPath : Invalid path provided.";
    }

    // Note: We don't need to handle the case of multiple consecutive separator chars.
    const bool startsWithSeparator = (path[0] == SEPARATOR_CHAR);
    size_t firstComponentEnd = path.find(SEPARATOR_CHAR, startsWithSeparator ? 1 : 0);
    if (firstComponentEnd == std::string::npos)
    {
        firstComponentEnd = path.size();
    }

    return path.substr(
        startsWithSeparator ? 1 : 0,
        firstComponentEnd - (startsWithSeparator ? 1 : 0));
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptUtils::RemoveFirstComponentFromPath(/*[in] */ const std::string& path)
{
    if (path == "")
    {
        LOG_ERROR("RemoveFirstComponentFromPath : Invalid path provided. path={}", path);
        throw "RemoveFirstComponentFromPath : Invalid path provided.";
    }

    // Note: We don't need to handle the case of multiple consecutive separator chars.
    const bool startsWithSeparator = (path[0] == SEPARATOR_CHAR);
    size_t firstComponentEnd = path.find(SEPARATOR_CHAR, startsWithSeparator ? 1 : 0);
    if (firstComponentEnd == std::string::npos)
    {
        return "";
    }

    return path.substr(firstComponentEnd + 1);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptUtils::GetRemotePathPrefix(const std::string& mountName)
{
    std::string remotePathPrefix = "";
    if (StorageInterceptConfig::GetInstance().GetRemoteStoreType(mountName) ==
        StorageInterceptConstants::ConfigValRemoteTypeBlob)
    {
        const bool isDynamicContainer = StorageInterceptConfig::GetInstance().IsDynamicContainerMount(mountName);
        const bool isIgnoreAccountName = StorageInterceptConfig::GetInstance().IsIgnoreAccountNameInCacheKey(mountName);
        if(!isIgnoreAccountName)
        {
            remotePathPrefix = StorageInterceptConfig::GetInstance().GetStorageAccountName(mountName) + "/";
        }

        if (!isDynamicContainer)
        {
            remotePathPrefix += StorageInterceptConfig::GetInstance().GetBlobContainerName(mountName) + "/";
        }
    }
    else if (StorageInterceptConfig::GetInstance().GetRemoteStoreType(mountName) ==
        StorageInterceptConstants::ConfigValRemoteTypeAdls)
    {
        remotePathPrefix = StorageInterceptConfig::GetInstance().GetAdlsHostName(mountName) + "/" +
            StorageInterceptConfig::GetInstance().GetAdlsClientId(mountName) + "/";
    }

    return remotePathPrefix;
}
//---------------------------------------------------------------------------------------------------------------------