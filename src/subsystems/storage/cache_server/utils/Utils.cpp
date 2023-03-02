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
string CacheRootFolder = ".";
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
void Initialize()
{
    mkdir(CacheRootFolder.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    mkdir(GetCacheFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    // TODO: cleanup left over files in temp folder
    mkdir(GetCacheTempFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

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
unsigned int GetHashBucket(const std::string& strKey)
{
    static thread_local hash<string> hasher;
    auto hash_val = hasher(strKey);
    return hash_val % NUM_MAPS;
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
        LOG_INFO("Failed to create temp file, could be due to UUID collision {}, errno: {}:{}", tempFilePath,
            errno, strerror(errno));
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