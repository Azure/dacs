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

    const char* findResult = strstr(path, basePath.c_str());
    if (findResult != path)
    {
        return false;
    }

    std::string pathStr(path);
    size_t relPathStartIdx = basePath.length();
    if (relPathStartIdx < pathStr.size())
    {
        if (pathStr[relPathStartIdx] == separatorChar)
        {
            relPathStartIdx++; // don't include separator char in relative path.
        }
        else
        {
            // basePath is substring of path but not its parent. Example basePath=/foo path=/foot/bar
            return false;
        }
    }
    outRelativePath = pathStr.substr(relPathStartIdx);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------