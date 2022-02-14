//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteClientTest.h"
#include "RemoteClientProvider.h"
#include "mock/MockRemoteClient.h"
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
    m_RemoteClient = std::move(RemoteClientProvider::GetRemoteClient());
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
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForDirPathEndingWithDirDelimiterTest)
{
    const std::string remoteDirPath = m_strRemoteDirPath + DIR_DELIMITER;
    FileAttributes attr;
    int nResult;
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
    }
    else
    {
        ASSERT_NO_THROW(
        {
            nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
        });
    }
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForDirPathEndingWithoutDirDelimiterTest)
{
    FileAttributes attr;
    int nResult;
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(m_strRemoteDirPath, attr);
    }
    else
    {
        ASSERT_NO_THROW(
        {
            nResult = m_RemoteClient->GetProperties(m_strRemoteDirPath, attr);
        });
    }
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForPrefixToValidPathWithoutDirDelimiterTest)
{
    FileAttributes attr;
    int nResult;
    const std::string remoteDirPath = m_strRemoteDirPath.substr(0, m_strRemoteDirPath.size() - 2);
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
        ASSERT_NE(nResult, 0);
    }
    else
    {
        ASSERT_ANY_THROW(
        {
            nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
        });
    }

    const std::string remoteFilePath = m_strRemoteFilePath.substr(0, m_strRemoteFilePath.size() - 2);
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
        ASSERT_NE(nResult, 0);
    }
    else
    {
        ASSERT_ANY_THROW(
        {
            nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
        });
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(RemoteClientTest, GetPropForPrefixToValidPathWithDirDelimiterTest)
{
    FileAttributes attr;
    int nResult;
    const std::string remoteDirPath = m_strRemoteDirPath.substr(0, m_strRemoteDirPath.size() - 2) + DIR_DELIMITER;
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
        ASSERT_NE(nResult, 0);
    }
    else
    {
        ASSERT_ANY_THROW(
        {
            nResult = m_RemoteClient->GetProperties(remoteDirPath, attr);
        });
    }

    const std::string remoteFilePath = m_strRemoteFilePath.substr(0, m_strRemoteFilePath.size() - 2) + DIR_DELIMITER;
    if (RemoteClientTest::IsInstanceOf<MockRemoteClient>(m_RemoteClient.get()))
    {
        nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
        ASSERT_NE(nResult, 0);
    }
    else
    {
        ASSERT_ANY_THROW(
        {
            nResult = m_RemoteClient->GetProperties(remoteFilePath, attr);
        });
    }
}
//---------------------------------------------------------------------------------------------------------------------
