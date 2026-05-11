///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageLibcApiWrapper.h"
#include "MockRemoteClientException.h"
#include "MockGetPropertiesApi.h"
#include "StorageInterceptConstants.h"
#include <sys/stat.h>
#include <sys/xattr.h>
//---------------------------------------------------------------------------------------------------------------------
MockGetPropertiesApi::MockGetPropertiesApi(
    const std::string& filePath,
    FileAttributes& attributes)
    : RemoteClientApiBase("MockGetPropertiesApi")
    , m_strFilePath(filePath)
    , m_attributes(attributes)
    , m_nResult(-1)
{
}
//---------------------------------------------------------------------------------------------------------------------
void MockGetPropertiesApi::PerformOperation()
{
    struct stat sb;
    m_nResult = stat(m_strFilePath.c_str(), &sb);

    m_attributes.isDir = ((sb.st_mode & S_IFMT) == S_IFDIR);
    m_attributes.size = sb.st_size;
    m_attributes.aTime = sb.st_atim;
    m_attributes.cTime = sb.st_ctim;
    m_attributes.mTime = sb.st_mtim;

    // Read dacsfileversion from file extended attributes (if set)
    char xattrBuf[40] = {};
    ssize_t xattrLen = getxattr(m_strFilePath.c_str(),
        (std::string("user.") + StorageInterceptConstants::BlobMetadataKeyVersion).c_str(),
        xattrBuf, sizeof(xattrBuf) - 1);
    if (xattrLen > 0)
    {
        xattrBuf[xattrLen] = '\0';
        m_attributes.version = xattrBuf;
    }
}
//---------------------------------------------------------------------------------------------------------------------
int MockGetPropertiesApi::GetResult() const
{
    return m_nResult;
}
//---------------------------------------------------------------------------------------------------------------------