//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtils.h"
#include <cstring>
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptUtils::CombinePath(
    const std::string& basePath,
    const std::string& relativePath)
{
    constexpr char separatorChar = '/';

    std::string result = basePath;
    if (basePath.back() == separatorChar)
    {
        result.pop_back();
    }

    if (relativePath.empty() || relativePath[0] == separatorChar)
    {
        result += relativePath;
    }
    else
    {
        result += separatorChar;
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
    constexpr char separatorChar = '/';

    int basePathIdx = 0;
    int pathIdx = 0;
    while (basePathIdx < basePath.size()) {
        if (path[pathIdx] == '\0') return false;

        if (basePath[basePathIdx] != path[pathIdx]) return false;

        basePathIdx++;

        while (path[pathIdx] == separatorChar && path[pathIdx+1] == separatorChar) pathIdx++;
        pathIdx++;
    }

    if (path[pathIdx] == '\0') {
        // basePath and path point to same directory.
        outRelativePath = "";
        return true;
    }

    if (path[pathIdx] != separatorChar) {
        // basePath is substring of path but not its parent. Example basePath=/foo path=/foot/bar
        return false;
    }

    // Don't include starting separator in relative path
    while (path[pathIdx] == separatorChar) pathIdx++;

    // Append rest of path to relative path (ignoring consecutive separators)
    while (path[pathIdx] != '\0') {
        outRelativePath += path[pathIdx];

        while (path[pathIdx] == separatorChar && path[pathIdx+1] == separatorChar) pathIdx++;
        pathIdx++;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------