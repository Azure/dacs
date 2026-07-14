///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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
string ServerHostName = "";
string CacheServerMetricPort = "";
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
string GetSnapshotFolderPath()
{
    return CacheRootFolder + "/snapshot";
}
//---------------------------------------------------------------------------------------------------------------------
void CleanupFolder(const string& folderPath)
{
    LOG_INFO("Cleaning up folder: {}", folderPath);

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(folderPath.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_type == DT_REG)
            {
                string filePath = folderPath + "/" + ent->d_name;
                if (remove(filePath.c_str()) != 0)
                {
                    LOG_ERROR("Failed to delete file: {}, errno: {}:{}", filePath, errno, strerror(errno));
                }
            }
            else if (ent->d_type == DT_DIR)
            {
                if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
                {
                    string subFolderPath = folderPath + "/" + ent->d_name;
                    CleanupFolder(subFolderPath);
                    if (rmdir(subFolderPath.c_str()) != 0)
                    {
                        LOG_ERROR("Failed to delete folder: {}, errno: {}:{}", subFolderPath, errno, strerror(errno));
                    }
                }
            }
            else
            {
                LOG_WARN("Unknown file type: {}", ent->d_type);
            }
        }
        closedir(dir);
    }
    else
    {
        LOG_ERROR("Failed to open folder: {}, errno: {}:{}", folderPath, errno, strerror(errno));
    }

    LOG_INFO("Finished cleaning up folder: {}", folderPath);
}
//---------------------------------------------------------------------------------------------------------------------
void ResetCacheFolder()
{
    LOG_INFO("Resetting cache folder: {}", GetCacheFolderPath());

    CleanupFolder(GetCacheFolderPath());

    mkdir(GetCacheTempFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    for (int i = 0; i < NUM_MAPS; i++)
    {
        mkdir((GetCacheFolderPath() + "/" + to_string(i)).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void InitializeUtils()
{
    mkdir(CacheRootFolder.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    mkdir(GetCacheFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    for (int i = 0; i < NUM_MAPS; i++)
    {
        mkdir((GetCacheFolderPath() + "/" + to_string(i)).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }

    mkdir(GetCacheTempFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    CleanupFolder(GetCacheTempFolderPath());

    mkdir(GetSnapshotFolderPath().c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

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

    char* serverHostName = std::getenv("CACHESERVER_HOSTNAME");
    if (serverHostName != NULL && strlen(serverHostName) > 0)
    {
        ServerHostName = serverHostName;
    }

    char* cacheServerMetricPort = std::getenv("CACHESERVER_METRICS_PORT");
    if (cacheServerMetricPort != NULL && strlen(cacheServerMetricPort) > 0)
    {
        char* pEndPtr = nullptr;
        long int nValue = std::strtol(cacheServerMetricPort, &pEndPtr, 10);
        if (pEndPtr == cacheServerMetricPort || nValue < 0 || nValue > 65535)
        {
            LOG_CRITICAL("Invalid port number for metrics: {}", cacheServerMetricPort);
            exit(-1);
        }
        CacheServerMetricPort = std::to_string(nValue);
        LOG_INFO("Metrics port: {}", CacheServerMetricPort);
    }
    else
    {
        LOG_CRITICAL("Metrics port not set");
        exit(-1);
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
string GetServerHostName()
{
    return ServerHostName;
}
//---------------------------------------------------------------------------------------------------------------------
string GetCacheServerMetricPort()
{
    return CacheServerMetricPort;
}
//---------------------------------------------------------------------------------------------------------------------
// Use hex encoding to handle filenames with special characters
string FileNameEncode(const string& fileName)
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
    int folderHash = GetHashBucket(fileName);
    return GetCacheFolderPath() + "/" + to_string(folderHash) + "/" + FileNameEncode(fileName);
}
//---------------------------------------------------------------------------------------------------------------------
bool ReadConfigFile(const string& filePath, string& content, const std::string& requestid)
{
    ifstream configFile(filePath);
    if (!configFile.is_open())
    {
        LOG_ERROR_REQID_STR("Failed to open configuration file: {}. Error: {}", requestid, filePath, strerror(errno));
        return false;
    }

    string tempContent;
    configFile >> tempContent;
    if (configFile.bad()) {
        LOG_ERROR_REQID_STR("I/O error while reading configuration file: {}. Error: {}", requestid, filePath, strerror(errno));
        return false;
    } else if (configFile.fail()) {
        LOG_ERROR_REQID_STR("Failed to read from configuration file: {}. Error: {}", requestid, filePath, strerror(errno));
        return false;
    }
    else if (!configFile.eof()) {
        LOG_ERROR_REQID_STR("Configuration file: {} has extra data.",requestid,  filePath);
        return false;
    }

    content = tempContent;
    configFile.close();
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool GetCacheServersHelper(::google::protobuf::RepeatedPtrField<::std::string>& serveraddresses, const std::string& requestid) 
{
    string cacheServerCountStr;
    //TODO: Periodically poll the file for better performance instead of read on every request
    if (!ReadConfigFile(CACHE_SERVER_COUNT_CONFIG_PATH, cacheServerCountStr, requestid))
    {
        return false;
    }

    uint32_t cacheServerCount;
    try  
    {
        cacheServerCount = stoi(cacheServerCountStr);
    }
    catch (const invalid_argument& e)
    {
        LOG_ERROR_REQID_STR("Invalid integer in configuration file: {} exception: {}",requestid, CACHE_SERVER_COUNT_CONFIG_PATH, e.what());
        return false;
    }
    catch (const out_of_range& e)
    {
        LOG_ERROR_REQID_STR("Integer out of range in configuration file: {} exception: {}",requestid, CACHE_SERVER_COUNT_CONFIG_PATH, e.what());
        return false;
    }

    if (cacheServerCount == 0)
    {
        LOG_ERROR_REQID_STR("Cache server count is 0", requestid);
        return false;
    }

    string cacheServerEndpointFormat;
    if (!ReadConfigFile(CACHE_SERVER_ENDPOINT_FORMAT_CONFIG_PATH, cacheServerEndpointFormat, requestid))
    {
        return false;
    }

    const string placeholder = "{}";
    size_t pos = cacheServerEndpointFormat.find(placeholder);
    if (pos == string::npos) 
    {
        LOG_ERROR_REQID_STR("Placeholder not found in endpoint format: {}", requestid, cacheServerEndpointFormat);
        return false;
    }
    for (uint32_t i = 0; i < cacheServerCount; i++)
    {
        string cacheServerEndpoint  = cacheServerEndpointFormat;
        cacheServerEndpoint.replace(pos, placeholder.length(), to_string(i));
        serveraddresses.Add()->assign(cacheServerEndpoint);
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------