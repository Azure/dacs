///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Utils.h"
#include "Constants.h"
#include "Logger.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <sys/types.h>
#include <dirent.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <errno.h>
#include <atomic>
#include <openssl/hmac.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
vector<unordered_set<string> *> LockedFilesSet;
vector<mutex *> LockedFilesSetMutex;
vector<unordered_map<string, CacheInfo> *> CacheInfoMap;
vector<mutex *> CacheInfoMapMutex;
vector<unordered_map<string, CacheAttributes> *> AttributesMap;
vector<mutex *> AttributesMutex;
string CacheRootFolder = ".";
uint64_t AttributeCacheLimit = DefaultAttributeCacheLimit;
atomic_uint_fast64_t TotalCachedAttributes = 0;
string AuthAccountName = "";
string AuthAccountKey = "";
bool AuthEnabled = false;
//---------------------------------------------------------------------------------------------------------------------
void SetCacheRootPath(const string& rootFolder)
{
    CacheRootFolder = rootFolder;
}
//---------------------------------------------------------------------------------------------------------------------
string GetCacheFolderPath()
{
    return CacheRootFolder + "/_cache";
}
//---------------------------------------------------------------------------------------------------------------------
string GetCacheTempFolderPath()
{
    return CacheRootFolder + "/_cache/temp_upload";
}
//---------------------------------------------------------------------------------------------------------------------
void SetAttributeCacheLimit(const uint64_t limitCacheEntries)
{
    AttributeCacheLimit = limitCacheEntries;
}
//---------------------------------------------------------------------------------------------------------------------
void Initialize()
{
    mkdir(CacheRootFolder.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    mkdir(GetCacheFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    // TODO: cleanup left over files in temp folder
    mkdir(GetCacheTempFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    for (int i = 0; i < NUM_MAPS; ++i)
    {
        LockedFilesSet.push_back(new unordered_set<string>());
        LockedFilesSetMutex.push_back(new mutex());

        CacheInfoMap.push_back(new unordered_map<string, CacheInfo>());
        CacheInfoMapMutex.push_back(new mutex());

        AttributesMap.push_back(new unordered_map<string, CacheAttributes>());
        AttributesMutex.push_back(new mutex());
    }

    char* authEnabled = std::getenv("CACHESERVER_AUTH_ENABLED");
    if (authEnabled != NULL && strcasecmp(authEnabled, "true") == 0)
    {
        AuthEnabled = true;
        char* accountName = std::getenv("CACHESERVER_AUTH_ACCOUNTNAME");
        char* accountKey = std::getenv("CACHESERVER_AUTH_ACCOUNTKEY");
        if (accountName == NULL || accountKey == NULL)
        {
            LOG_CRITICAL("Auth is enabled but Account name or key not set");
            exit(-1);
        }
        AuthAccountName = accountName;
        AuthAccountKey = accountKey;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool IsAuthEnabled()
{
    return AuthEnabled;
}
//---------------------------------------------------------------------------------------------------------------------
bool ValidateAuthSignature(const string& clientAccountName, const string& clientSignature, const string& message,
    time_t timestamp)
{
    time_t currentTimeStamp = time(NULL);
    
    // Allowing only 2 minutes drift in timestamp
    if (abs(currentTimeStamp - timestamp) > 120)
    {
        LOG_WARN("Timestamp not within bound, server ts: {}, client ts: {}", currentTimeStamp, timestamp);
        return false;
    }

    if (clientAccountName != AuthAccountName)
    {
        LOG_WARN("Account name does not match, client: {}, server: {}", clientAccountName, AuthAccountName);
        return false;
    }

    string hashValue(EVP_MAX_MD_SIZE, '\0');
    unsigned int hashLen = EVP_MAX_MD_SIZE;
    
    if (!HMAC(EVP_sha256(), AuthAccountKey.data(), AuthAccountKey.length(),
        (const unsigned char *)message.data(), message.length(), (unsigned char *)hashValue.data(), &hashLen))
    {
        LOG_ERROR("Failed to generate HMAC signature");
        return false;
    }

    if (clientSignature != hashValue.substr(0, hashLen))
    {
        LOG_WARN("Auth signatures don't match");
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
CacheInfo GetAndInitCacheStatus(const string& fileName)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    CacheInfo prevCacheStatus;

    lock_guard lockGuard(*CacheInfoMapMutex[index]);
    auto iter = CacheInfoMap[index]->find(fileName);
    if (iter == CacheInfoMap[index]->end())
    {
        CacheInfoMap[index]->insert(make_pair(fileName,
            CacheInfo(0, ::google::protobuf::Map<std::string, std::string>(), INITIATED)));
    }
    else
    {
        prevCacheStatus = iter->second;
    }
    return prevCacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
CacheInfo GetCacheStatus(const string& fileName)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    CacheInfo prevCacheStatus;
    lock_guard lockGuard(*CacheInfoMapMutex[index]);
    auto iter = CacheInfoMap[index]->find(fileName);
    if (iter != CacheInfoMap[index]->end())
    {
        prevCacheStatus = iter->second;
    }
    return prevCacheStatus;    
}
//---------------------------------------------------------------------------------------------------------------------
bool UpdateCacheStatus(const std::string& fileName, bool ignoreLock, CacheInfo cacheInfo)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    lock_guard lockGuard(*CacheInfoMapMutex[index]);
    auto iter = CacheInfoMap[index]->find(fileName);
    if (iter != CacheInfoMap[index]->end())
    {
        iter->second = cacheInfo;
    }
    else
    {
        if (ignoreLock)
        {
            CacheInfoMap[index]->insert(make_pair(fileName, cacheInfo));
        }
        else
        {
            LOG_WARN("Cache not initiated for file: {}", fileName);
            return false;
        }
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool GetLockForUpload(const string& fileName)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    bool ownFile = false;
    lock_guard lockGuard(*LockedFilesSetMutex[index]);
    if (LockedFilesSet[index]->find(fileName) == LockedFilesSet[index]->end())
    {
        LockedFilesSet[index]->insert(fileName);
        ownFile = true;
    }
    return ownFile;
}
//---------------------------------------------------------------------------------------------------------------------
// Use hex encoding to handle filenames with special characters
string fileEncode(const string& fileName)
{
    std::stringstream ss;
    for(auto i = 0; i < fileName.size(); i++)
    {
        ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(fileName[i]);
    }
    return ss.str();
}
//---------------------------------------------------------------------------------------------------------------------
// Generate 32 bytes random string
string GetUuid()
{
    std::stringstream ss;
    for(auto i = 0; i < 8; i++)
    {
        ss << std::setfill('0') << std::setw(4) << std::hex << rand();
    }
    return ss.str();
}
//---------------------------------------------------------------------------------------------------------------------
string GetCacheTempPath(const string& fileName)
{
    // Try a few times since there can be collisions 
    for (int attempt = 0; attempt < 10; attempt++)
    {
        string tempFilePath = GetCacheTempFolderPath() + "/" + GetUuid();

        // Attempt to create the file atomically 
        int fileFd = open(tempFilePath.c_str(), O_CREAT | O_EXCL, 0644);
        if (fileFd >= 0)
        {
            // Successfully created a new file, return this path.
            close(fileFd);
            return tempFilePath;
        }
        LOG_INFO("Failed to create temp file, could be due to UUID collision {}, errno: {}", tempFilePath, errno);
    }
    LOG_ERROR("Couldn't create a cache temp file for {}", fileName);
    return "";
}
//---------------------------------------------------------------------------------------------------------------------
string GetCachePath(const string& fileName)
{
    return GetCacheFolderPath() + "/" + fileEncode(fileName);
}
//---------------------------------------------------------------------------------------------------------------------
bool GetAndInitAttribute(const std::string& fileName, CacheAttributes& fileAttributes)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    lock_guard lockGuard(*AttributesMutex[index]);
    auto iter = AttributesMap[index]->find(fileName);
    if (iter == AttributesMap[index]->end())
    {
        AttributesMap[index]->insert(make_pair(fileName, CacheAttributes()));
        return false;
    }
    else
    {
        fileAttributes = iter->second;
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool GetAttribute(const std::string& fileName, CacheAttributes& fileAttributes)
{
    static hash<string> hasher;
    auto hash_val = hasher(fileName);
    auto index = hash_val % NUM_MAPS;

    lock_guard lockGuard(*AttributesMutex[index]);
    auto iter = AttributesMap[index]->find(fileName);
    if (iter == AttributesMap[index]->end())
    {
        return false;
    }
    else
    {
        fileAttributes = iter->second;
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool PutAttributes(const cache::PutAttributeRequest& putAttributeRequest)
{
    if ((TotalCachedAttributes.fetch_add(putAttributeRequest.fileattributes_size()) +
            putAttributeRequest.fileattributes_size()) > AttributeCacheLimit)
    {
        LOG_WARN("TotalCachedAttributes: {}, exceeds limit: {}", TotalCachedAttributes, AttributeCacheLimit);
        TotalCachedAttributes.fetch_sub(putAttributeRequest.fileattributes_size());
        return false;
    }

    static hash<string> hasher;
    if (putAttributeRequest.fileattributes_size() == 1)
    {
        auto &fileAttr = putAttributeRequest.fileattributes(0);
        const string& fileName = fileAttr.filename();
        auto hash_val = hasher(fileName);
        auto index = hash_val % NUM_MAPS;

        lock_guard lockGuard(*AttributesMutex[index]);
        (*AttributesMap[index])[fileName] = CacheAttributes(
            fileAttr.fileattribute().isdir(),
            fileAttr.fileattribute().filesize(),
            fileAttr.fileattribute().accessedtime(),
            fileAttr.fileattribute().modifiedtime());
        return true;
    }
    
    // Add multiple entries and optimize for locking
    vector<vector<const ::cache::FileAttributes*>> vecAttrEntries(NUM_MAPS);
    for (int iter = 0; iter < putAttributeRequest.fileattributes_size(); iter++)
    {
        const ::cache::FileAttributes* fileAttr = &putAttributeRequest.fileattributes(iter);
        auto hash_val = hasher(fileAttr->filename());
        auto index = hash_val % NUM_MAPS;
        vecAttrEntries[index].push_back(fileAttr);
    }

    for (int index = 0; index < NUM_MAPS; index++)
    {
        if (vecAttrEntries[index].empty())
        {
            continue;
        }

        lock_guard lockGuard(*AttributesMutex[index]);
        for (auto fileAttr : vecAttrEntries[index])
        {
            (*AttributesMap[index])[fileAttr->filename()] = CacheAttributes(
                fileAttr->fileattribute().isdir(),
                fileAttr->fileattribute().filesize(),
                fileAttr->fileattribute().accessedtime(),
                fileAttr->fileattribute().modifiedtime());
        }
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------