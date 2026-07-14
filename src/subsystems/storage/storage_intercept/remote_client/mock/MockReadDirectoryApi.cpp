///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageLibcApiWrapper.h"
#include "MockRemoteClientException.h"
#include "MockReadDirectoryApi.h"
#include "StorageInterceptConstants.h"
#include <cstring>
#include <dirent.h>
#include <sys/xattr.h>
//---------------------------------------------------------------------------------------------------------------------
MockReadDirectoryApi::MockReadDirectoryApi(
    const std::string& dirPath,
    const std::string& continuationToken,
    std::vector<DirectoryEntry>& dirEntries)
    : RemoteClientApiBase("MockReadDirectoryApi")
    , m_strDirPath(dirPath)
    , m_strContToken(continuationToken)
    , m_vecDirEntries(dirEntries)
{
}
//---------------------------------------------------------------------------------------------------------------------
void MockReadDirectoryApi::PerformOperation()
{
    void* dirp = StorageLibcApiWrapper::opendir(m_strDirPath.c_str());
    if (!dirp)
    {
        throw MockRemoteClientException("Failed to open directory at path " + m_strDirPath);
    }

    uint32_t nExistingEntries = m_strContToken.empty() ? 0 : (uint32_t)std::atoi(m_strContToken.c_str());

    bool reachedEod = false;
    uint32_t nEntriesAdded = 0;
    for (uint32_t i=0; i < 5; i++)
    {
        struct dirent *dir = (struct dirent *)StorageLibcApiWrapper::readdir64(dirp);
        if (!dir)
        {
            reachedEod = true;
            break;
        }

        /* Skipping the . and .. entries */
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
        {
            continue;
        }

        m_vecDirEntries.push_back(DirectoryEntry());
        m_vecDirEntries.back().d_ino = D_INO_VAL;
        m_vecDirEntries.back().d_off = m_vecDirEntries.size() - 1;
        m_vecDirEntries.back().d_reclen = sizeof(DirectoryEntry);
        m_vecDirEntries.back().d_type = dir->d_type;
        strncpy(m_vecDirEntries.back().d_name, dir->d_name, D_NAME_LEN);

        // Read dacsfileversion from file extended attributes (if set)
        std::string filePath = m_strDirPath + "/" + dir->d_name;
        char xattrBuf[D_VERSION_LEN] = {};
        ssize_t xattrLen = getxattr(filePath.c_str(),
            (std::string("user.") + StorageInterceptConstants::BlobMetadataKeyVersion).c_str(),
            xattrBuf, sizeof(xattrBuf) - 1);
        if (xattrLen > 0)
        {
            xattrBuf[xattrLen] = '\0';
            strncpy(m_vecDirEntries.back().d_version, xattrBuf, D_VERSION_LEN - 1);
            m_vecDirEntries.back().d_version[D_VERSION_LEN - 1] = '\0';
        }
        else
        {
            m_vecDirEntries.back().d_version[0] = '\0';
        }
        nEntriesAdded++;
    }

    if (!reachedEod)
    {
        m_strNewContToken = std::to_string(nExistingEntries + nEntriesAdded);
    }

    if (StorageLibcApiWrapper::closedir(dirp) != 0)
    {
        throw MockRemoteClientException("Failed to close directory at path " + m_strDirPath);
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::string MockReadDirectoryApi::GetResult() const
{
    return m_strNewContToken;
}
//---------------------------------------------------------------------------------------------------------------------