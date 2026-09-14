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

#include "StorageInterceptUtils.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include <cstring>
#include <cstdlib>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
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
        const bool isDynamicAccount = StorageInterceptConfig::GetInstance().IsDynamicAccountMount(mountName);
        const bool isDynamicContainer = StorageInterceptConfig::GetInstance().IsDynamicContainerMount(mountName);
        const bool isIgnoreAccountName = StorageInterceptConfig::GetInstance().IsIgnoreAccountNameInCacheKey(mountName);

        if (isDynamicAccount)
        {
            return "";
        }

        if(isIgnoreAccountName)
        {
            remotePathPrefix = std::string(StorageInterceptConstants::CacheKeyAccountNameOverride) + "/";
        }
        else
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
std::string StorageInterceptUtils::GenerateUUID()
{
    static thread_local boost::uuids::random_generator uuidGenerator;
    boost::uuids::uuid uuid = uuidGenerator();
    return boost::uuids::to_string(uuid);
}
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    char AsciiLower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    bool IsHostChar(char c)
    {
        // Only unreserved hostname characters. This rejects a path ('/'), userinfo ('@'),
        // port (':'), query/fragment, and any whitespace or control byte that could point
        // the request at a different host.
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '-';
    }

    // Case-insensitive suffix test. Compares in place so neither operand has to be
    // copied into a lowercased temporary. AsciiLower is used rather than std::tolower
    // because std::tolower is locale-dependent and is undefined for negative char
    // values, either of which could change which hosts are accepted.
    bool EndsWithIgnoreCase(const std::string& value, const std::string& suffix)
    {
        if (suffix.size() > value.size())
        {
            return false;
        }
        const size_t offset = value.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i)
        {
            if (AsciiLower(value[offset + i]) != AsciiLower(suffix[i]))
            {
                return false;
            }
        }
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::IsAllowedEndpointHost(
    const std::string& host,
    const std::vector<std::string>& allowedSuffixes)
{
    if (host.empty())
    {
        return false;
    }

    for (char c : host)
    {
        if (!IsHostChar(c))
        {
            return false;
        }
    }

    for (const std::string& suffix : allowedSuffixes)
    {
        if (suffix.empty())
        {
            continue;
        }

        // Entries not starting with '.' are exact hostnames (e.g. legacy single-host AAD
        // authorities that have no meaningful subdomain family), matched in full rather than
        // as a suffix so they can't be used to admit unrelated sibling services.
        if (suffix.front() != '.')
        {
            if (host.size() == suffix.size() && EndsWithIgnoreCase(host, suffix))
            {
                return true;
            }
            continue;
        }

        // Require a non-empty label before the suffix, so a bare suffix such as
        // ".blob.core.windows.net" is not accepted as a host in its own right.
        if (host.size() <= suffix.size())
        {
            continue;
        }
        if (EndsWithIgnoreCase(host, suffix))
        {
            return true;
        }
    }

    return false;
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::IsHttpsUrlWithAllowedHost(
    const std::string& url,
    const std::vector<std::string>& allowedHostSuffixes)
{
    const std::string scheme = "https://";
    if (url.size() <= scheme.size())
    {
        return false;
    }
    for (size_t i = 0; i < scheme.size(); ++i)
    {
        if (AsciiLower(url[i]) != scheme[i])
        {
            return false;
        }
    }

    const size_t hostStart = scheme.size();
    const size_t hostEnd = url.find('/', hostStart);
    const std::string host = (hostEnd == std::string::npos)
        ? url.substr(hostStart)
        : url.substr(hostStart, hostEnd - hostStart);

    return IsAllowedEndpointHost(host, allowedHostSuffixes);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::LooksLikeJwt(const std::string& token)
{
    int dots = 0;
    size_t segmentLength = 0;
    for (char c : token)
    {
        if (c == '.')
        {
            if (segmentLength == 0)
            {
                return false;
            }
            if (++dots > 2)
            {
                return false;
            }
            segmentLength = 0;
            continue;
        }
        const bool base64Url = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '=';
        if (!base64Url)
        {
            return false;
        }
        ++segmentLength;
    }
    return dots == 2 && segmentLength > 0;
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::IsAllowedBlobHost(const std::string& host)
{
    std::vector<std::string> allowedSuffixes = {
        ".blob.core.windows.net",        // Azure public cloud
        ".blob.core.usgovcloudapi.net",  // Azure US Government
        ".blob.core.chinacloudapi.cn",   // Azure China
        ".blob.core.cloudapi.de",        // Azure Germany
        ".blob.core.microsoft.scloud",   // Microsoft secure cloud
        ".blob.core.eaglex.ic.gov",      // US national security cloud
    };

    // Honor the operator-configured endpoint suffix so custom/sovereign deployments keep working.
    const char* endpointSuffix = std::getenv("AZURE_BLOB_ENDPOINT_SUFFIX");
    if (endpointSuffix != nullptr && endpointSuffix[0] != '\0')
    {
        std::string suffix = endpointSuffix;
        if (suffix.front() != '.')
        {
            suffix.insert(suffix.begin(), '.');
        }
        allowedSuffixes.push_back(suffix);
    }

    return IsAllowedEndpointHost(host, allowedSuffixes);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptUtils::IsAllowedAuthorityUrl(const std::string& url)
{
    static const std::vector<std::string> allowedSuffixes = {
        ".microsoftonline.com",  // Azure AD public cloud
        ".microsoftonline.us",   // Azure AD US Government
        ".chinacloudapi.cn",     // Azure AD China
        ".microsoftonline.de",   // Azure AD Germany
        // Legacy single-host AAD authorities. Matched as exact hostnames (no leading '.')
        // rather than as a ".windows.net" suffix, which would also admit unrelated
        // *.windows.net services (e.g. management/database/servicebus endpoints).
        "login.windows.net",
        "sts.windows.net",
    };

    return IsHttpsUrlWithAllowedHost(url, allowedSuffixes);
}
//---------------------------------------------------------------------------------------------------------------------