//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteClientTest.h"
#include "RemoteClientProvider.h"
#include "StorageInterceptConfig.h"
#include "mock/MockRemoteClient.h"
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
RemoteClientTest::RemoteClientTest()
    : TestBase()
    , m_RemoteClient(nullptr)
    , DIR_DELIMITER("/")
{
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteClientTest::SetUp()
{
    m_RemoteClient = std::move(RemoteClientProvider::GetRemoteClient(""));
}
//---------------------------------------------------------------------------------------------------------------------
template<typename Base, typename T>
bool RemoteClientTest::IsInstanceOf(const T* pType)
{
    return dynamic_cast<const Base*>(pType) != nullptr;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, ReadChunkFullFileTest)
{
    std::vector<char> writeBuf(REMOTE_FILE_SIZE);

    ssize_t nBytesRead = m_RemoteClient->ReadChunkSync(
        m_strRemoteFilePath,
        writeBuf.data(),
        writeBuf.size(),
        0 /* offset */);
    ASSERT_EQ(nBytesRead, REMOTE_FILE_SIZE);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropertiesTest)
{
    FileAttributes fileAttr;
    int result = m_RemoteClient->GetProperties(m_strRemoteFilePath, fileAttr);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(fileAttr.isDir, false);
    ASSERT_EQ(fileAttr.size, REMOTE_FILE_SIZE);
    if (m_bUsingMock)
    {
        // Mock client reads dacsfileversion from file xattr set during setup
        ASSERT_EQ(fileAttr.version, "mock-test-version-001");
    }
    else
    {
        // Real Azure blobs without dacsfileversion fall back to etag
        ASSERT_FALSE(fileAttr.version.empty());
    }

    FileAttributes dirAttr;
    result = m_RemoteClient->GetProperties(m_strRemoteDirPath, dirAttr);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(dirAttr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, ReadDirectoryTest)
{
    std::vector<DirectoryEntry> vecDirEntries;

    std::string newContToken = m_RemoteClient->ReadDirectory(m_strRemoteDirPath, "", vecDirEntries);
    ASSERT_EQ(newContToken, "");
    ASSERT_EQ(vecDirEntries.size(), 2);
    for (const auto& entry : vecDirEntries)
    {
        if (entry.d_type == DT_DIR)
        {
            // Directories never have version
            ASSERT_EQ(entry.d_version[0], '\0');
        }
        else if (m_bUsingMock)
        {
            // Mock client reads dacsfileversion from file xattr
            ASSERT_STREQ(entry.d_version, "mock-test-version-001");
        }
        else
        {
            // Real Azure blobs without dacsfileversion fall back to etag
            ASSERT_NE(entry.d_version[0], '\0');
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForDirPathEndingWithDirDelimiterTest)
{
    const std::string remoteDirPath = m_strRemoteDirPath + DIR_DELIMITER;
    FileAttributes attr;
    int nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForDirPathEndingWithoutDirDelimiterTest)
{
    FileAttributes attr;
    int nResult = m_RemoteClient->GetProperties(m_strRemoteDirPath, attr);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForPrefixToValidPathWithoutDirDelimiterTest)
{
    FileAttributes attr;
    int nResult;

    const std::string remoteDirPath = m_strRemoteDirPath.substr(0, m_strRemoteDirPath.size() - 2);
    nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
    ASSERT_NE(nResult, 0);

    const std::string remoteFilePath = m_strRemoteFilePath.substr(0, m_strRemoteFilePath.size() - 2);
    nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
    ASSERT_NE(nResult, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForPrefixToValidPathWithDirDelimiterTest)
{
    FileAttributes attr;
    int nResult;

    const std::string remoteDirPath = m_strRemoteDirPath.substr(0, m_strRemoteDirPath.size() - 2) + DIR_DELIMITER;
    nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
    ASSERT_NE(nResult, 0);

    const std::string remoteFilePath = m_strRemoteFilePath.substr(0, m_strRemoteFilePath.size() - 2) + DIR_DELIMITER;
    nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
    ASSERT_NE(nResult, 0);
}
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
