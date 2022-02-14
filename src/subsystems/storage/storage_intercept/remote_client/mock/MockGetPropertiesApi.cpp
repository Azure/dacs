///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageDlsymWrapper.h"
#include "MockRemoteClientException.h"
#include "MockGetPropertiesApi.h"
#include <sys/stat.h>
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
}
//---------------------------------------------------------------------------------------------------------------------
int MockGetPropertiesApi::GetResult() const
{
    return m_nResult;
}
//---------------------------------------------------------------------------------------------------------------------