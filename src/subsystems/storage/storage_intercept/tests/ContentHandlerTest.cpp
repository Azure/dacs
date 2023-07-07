//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ContentHandlerTest.h"
#include "ContentHandler.h"
#include "ErrorHandler.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptUtils.h"
#include <fcntl.h>
#include <iostream>
#include <functional>
#include <thread>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
//---------------------------------------------------------------------------------------------------------------------
ContentHandlerTest::ContentHandlerTest()
    : TestBase()
{
    const char* optionalTestCaseDir = std::getenv("STORAGE_INTERCEPT_TESTCASES_DIR");
    m_strTestCaseDir = std::string(
        optionalTestCaseDir == nullptr
        ? "./../../../../../../../../src/subsystems/storage/storage_intercept/tests/testcases"
        : optionalTestCaseDir);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::TearDown()
{
    if (m_bConfigUpdated)
    {
        StorageInterceptConfig::GetInstance().ReloadConfig();
        ContentHandler::GetInstance().ForkHandlerPrepare();
        ContentHandler::GetInstance().ForkHandlerParent();
        m_bConfigUpdated = false;
    }
}
//---------------------------------------------------------------------------------------------------------------------
template <typename task_t>
void ContentHandlerTest::ExecuteParallelWithArgs(
    std::function<void(task_t)> lambda,
    std::vector<task_t> tasks,
    int parallelism)
{
    size_t numTasks = tasks.size();
    if (numTasks == 0)
    {
        return;
    }
    size_t totalExecuted = 0;
    while(totalExecuted < numTasks)
    {
        std::vector<std::thread> parallelThreads;
        for(uint32_t idx = 0; idx < parallelism; idx++)
        {
            size_t taskId = totalExecuted + idx;
            if (taskId == numTasks)
            {
                break;
            }
            task_t task = tasks[taskId];
            parallelThreads.push_back(std::thread([task, lambda]
            {
                lambda(task);
            }));
        }
        for(uint32_t idx = 0; idx < parallelThreads.size(); idx++)
        {
            parallelThreads[idx].join();
        }
        totalExecuted += parallelThreads.size();
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::OpenReadAndCloseCheck(OpenReadAndCloseTask task)
{
    // ContentHandler should intercept open() and return a file handle.
    const int fd = ContentHandler::GetInstance().HandleOpen64(task.filePath.c_str(), task.openFlags);
    ASSERT_NE(fd, -1);

    std::vector<char> readBuf(task.readSize);

    // Reading the entire file using the file handle should work.
    ssize_t nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), task.readSize);
    ASSERT_EQ(nBytesRead, task.readSize);

    // We should not be able to read more data now that we have reached end of file.
    nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), 1);
    ASSERT_EQ(nBytesRead, 0);

    // Closing the file handle should work.
    const int closeRet = ContentHandler::GetInstance().HandleClose(fd);
    ASSERT_EQ(closeRet, 0);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::ReaddirCheck(ReaddirTask task)
{
    // ContentHandler should intercept opendir() and return a dir stream.
    void* dirp = ContentHandler::GetInstance().HandleOpendir(task.dirPath.c_str());
    ASSERT_NE(dirp, nullptr);

    // Read the dir contents. There should be 4 entries (".", "..", testFile and testDir)
    uint32_t nFiles = 0, nDirs = 0;
    struct dirent *dirEntry;
    while ((dirEntry = (struct dirent* )ContentHandler::GetInstance().HandleReaddir(dirp, true)) != NULL) {
        if (dirEntry->d_type == DT_DIR)
        {
            nDirs++;
        }
        else if (dirEntry->d_type == DT_REG)
        {
            nFiles++;
        }
    }
    ASSERT_EQ(nFiles, task.numFiles);
    ASSERT_EQ(nDirs, task.numDirs);

    // Closing the dir stream should work.
    const int closeRet = ContentHandler::GetInstance().HandleClosedir(dirp);
    ASSERT_EQ(closeRet, 0);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::GetPropertiesCheck(GetPropertiesTask task)
{
    struct stat64 dirAttr;
    // XStat for dir should work
    int xstatRet = ContentHandler::GetInstance().HandleXstat64(1, task.dirPath.c_str(), &dirAttr);
    ASSERT_EQ(xstatRet, 0);
    ASSERT_EQ(dirAttr.st_mode & S_IFMT, S_IFDIR);

    struct stat64 fileAttr;
    // XStat for file should work
    xstatRet = ContentHandler::GetInstance().HandleXstat64(1, task.filePath.c_str(), &fileAttr);
    ASSERT_EQ(xstatRet, 0);
    ASSERT_EQ(fileAttr.st_mode & S_IFMT, S_IFREG);
    ASSERT_EQ(fileAttr.st_size, task.fileSize);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenReadCloseOneThreadTest)
{
    OpenReadAndCloseTask task;
    task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    task.openFlags = O_RDONLY;
    task.readSize = REMOTE_FILE_SIZE;

    // Execute OpenReadAndCloseCheck with the task parameters
    OpenReadAndCloseCheck(task);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenFileNonExistentTest)
{
    std::string nonExistentPath = m_strInterceptPath + "/non-existent-file";

    // Open64 for non-existent file should return proper error
    int openRet = ContentHandler::GetInstance().HandleOpen64(nonExistentPath.c_str(), O_RDONLY);
    ASSERT_EQ(openRet, -1);
    ASSERT_EQ(errno, ENOENT);

    // Open for non-existent file should return proper error
    openRet = ContentHandler::GetInstance().HandleOpen(nonExistentPath.c_str(), O_RDONLY);
    ASSERT_EQ(openRet, -1);
    ASSERT_EQ(errno, ENOENT);
}
//---------------------------------------------------------------------------------------------------------------------
// This test requires usage of a pre-existing config that uses blob
TEST_F(ContentHandlerTest, OpenFileRemoteBlobErrorTest)
{
    // Only enable this test when using a config that uses blob/not mock
    // Here, we assume that the config provided has blob enabled
    // Also only run this test when cache is disabled to exercise remote blob failure codepath
    bool bCacheEnabled = StorageInterceptConfig::GetInstance().GetCacheEnable("" /*mountName*/);
    if(m_bUsingMock || bCacheEnabled)
    {
        std::cout << "Skipping test as config provided does not use blob / has cache enabled" << std::endl;
        return;
    }

    // Set incorrect SAS token and force contenthandler to reload config
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobSASToken, "incorrect-token");
    ContentHandler::GetInstance().ForkHandlerPrepare();
    ContentHandler::GetInstance().ForkHandlerParent();

    ErrorHandler::GetInstance().ResetState();
    errno = 0;
    std::string filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;

    std::thread t1([&](){
        int fd = ContentHandler::GetInstance().HandleOpen(filePath.c_str(), O_RDONLY);
        ASSERT_EQ(fd, -1);
        char* error = ContentHandler::GetInstance().HandleGetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(EIO, errno);

        fd = -2;
        ::free(error);
        error = nullptr;
        errno = 0;

        fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
        ASSERT_EQ(fd, -1);
        error = ContentHandler::GetInstance().HandleGetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(EIO, errno);
        ::free(error);
    });

    std::thread t2([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        char* error = ContentHandler::GetInstance().HandleGetLastError();
        ASSERT_EQ(nullptr, error);
        ASSERT_NE(EIO, errno);

        int fd = ContentHandler::GetInstance().HandleOpen(filePath.c_str(), O_RDONLY);
        ASSERT_EQ(fd, -1);
        error = ContentHandler::GetInstance().HandleGetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(EIO, errno);
        ::free(error);

        fd = -2;
        errno = 0;
        error = nullptr;

        fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
        ASSERT_EQ(fd, -1);
        error = ContentHandler::GetInstance().HandleGetLastError();
        ASSERT_NE(nullptr, error);
        ASSERT_EQ(EIO, errno);
        ::free(error);
    });

    t1.join();
    t2.join();
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, ReaddirOneThreadTest)
{
    ReaddirTask task;
    task.numFiles = 1;
    task.numDirs = 3;
    task.dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;

    // Execute ReaddirCheck with the task parameters
    ReaddirCheck(task);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, GetPropertiesOneThreadTest)
{
    GetPropertiesTask task;
    task.dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
    task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    task.fileSize = REMOTE_FILE_SIZE;

    // Execute GetPropertiesCheck with the task parameters
    GetPropertiesCheck(task);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, GetPropertiesNonExistentTest)
{
    std::vector<std::string> nonExistentPaths{
        m_strInterceptPath + "/non-existent-file",
        m_strInterceptPath + "/" + m_strRemoteFilePath.substr(0, m_strRemoteFilePath.size() - 1),
        m_strInterceptPath + '/' + m_strRemoteDirPath.substr(0, m_strRemoteDirPath.size() - 1) + '/'
    };
    struct stat64 fileAttr;

    for (const std::string nonExistentPath : nonExistentPaths)
    {
        // XStat for non-existent file should return proper error
        errno = 0;
        int xstatRet = ContentHandler::GetInstance().HandleXstat64(
            1, nonExistentPath.c_str(), &fileAttr);
        ASSERT_EQ(xstatRet, -1);
        ASSERT_EQ(errno, ENOENT);
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, GetPropertiesFileAsDirectoryTest)
{
    std::string dirPath = m_strInterceptPath + '/' + m_strRemoteFilePath + '/';
    struct stat64 dirAttr;

    // XStat for non-existent file should return proper error
    errno = 0;
    int xstatRet = ContentHandler::GetInstance().HandleXstat64(
        1, dirPath.c_str(), &dirAttr);
    ASSERT_EQ(xstatRet, -1);
    ASSERT_EQ(errno, ENOTDIR);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenReadAndCloseManyThreadsTest)
{
    std::vector<OpenReadAndCloseTask> tasks;
    const int nParallelism = 50;
    if (m_bUsingMock)
    {
        for(int idx = 0; idx < nParallelism; idx++)
        {
            OpenReadAndCloseTask task;
            task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
            task.openFlags = O_RDONLY;
            task.readSize = REMOTE_FILE_SIZE;
            tasks.push_back(task);
        }
    }
    else
    {
        std::ifstream testCaseStream(
        StorageInterceptUtils::CombinePath(m_strTestCaseDir, "file_paths_sizes.txt"),
        std::ios::in);
        std::string entry;
        while(std::getline(testCaseStream, entry))
        {
            auto delimiterIndex = entry.rfind(',');
            if (delimiterIndex == std::string::npos)
            {
                continue;
            }
            OpenReadAndCloseTask task;
            task.filePath = entry.substr(0, delimiterIndex);
            task.readSize = static_cast<size_t>(::atoi(entry.substr(delimiterIndex + 1).c_str()));
            task.openFlags = O_RDONLY;
            tasks.push_back(task);
        }
        testCaseStream.close();
    }

    std::function<void(OpenReadAndCloseTask)> lambda =
        std::bind(
            &ContentHandlerTest::OpenReadAndCloseCheck,
            this,
            std::placeholders::_1);
    ExecuteParallelWithArgs(lambda, tasks, nParallelism);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, ReaddirManyThreadsTest)
{
    std::vector<ReaddirTask> tasks;
    const int nParallelism = 50;
    if (m_bUsingMock)
    {
        for(int idx = 0; idx < nParallelism; idx++)
        {
            ReaddirTask task;
            task.dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
            task.numFiles = 1;
            task.numDirs = 3;
            tasks.push_back(task);
        }
    }
    else
    {
        std::ifstream testCaseStream(
        StorageInterceptUtils::CombinePath(m_strTestCaseDir, "directory_paths.txt"),
        std::ios::in);
        std::string entry;
        while(std::getline(testCaseStream, entry))
        {
            auto delimiterIndex = entry.rfind(',');
            if (delimiterIndex == std::string::npos)
            {
                continue;
            }
            ReaddirTask task;
            task.dirPath = entry.substr(0, delimiterIndex);
            task.numFiles = static_cast<size_t>(::atoi(entry.substr(delimiterIndex + 1).c_str()));
            task.numDirs = static_cast<size_t>(2);
            tasks.push_back(task);
        }
        testCaseStream.close();
    }
    std::function<void(ReaddirTask)> lambda =
        std::bind(
            &ContentHandlerTest::ReaddirCheck,
            this,
            std::placeholders::_1);
    ExecuteParallelWithArgs(lambda, tasks, nParallelism);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, GetPropertiesManyThreadsTest)
{
    std::vector<GetPropertiesTask> tasks;
    const int nParallelism = 50;
    if (m_bUsingMock)
    {
        for(int idx = 0; idx < nParallelism; idx++)
        {
            GetPropertiesTask task;
            task.dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
            task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
            task.fileSize = REMOTE_FILE_SIZE;
            tasks.push_back(task);
        }
    }
    else
    {
        std::ifstream testCaseStream(
        StorageInterceptUtils::CombinePath(m_strTestCaseDir, "file_paths_sizes.txt"),
        std::ios::in);
        std::string entry;
        while(std::getline(testCaseStream, entry))
        {
            auto delimiterIndex = entry.rfind(',');
            if (delimiterIndex == std::string::npos)
            {
                continue;
            }
            GetPropertiesTask task;
            task.filePath = entry.substr(0, delimiterIndex);
            task.fileSize = static_cast<size_t>(::atoi(entry.substr(delimiterIndex + 1).c_str()));
            const char DIR_DELIMITER = '/';
            auto dirSeparatorIdx = task.filePath.rfind(DIR_DELIMITER);
            task.dirPath = task.filePath.substr(0, dirSeparatorIdx);
            tasks.push_back(task);
        }
        testCaseStream.close();
    }

    std::function<void(GetPropertiesTask)> lambda =
        std::bind(
            &ContentHandlerTest::GetPropertiesCheck,
            this,
            std::placeholders::_1);
    ExecuteParallelWithArgs(lambda, tasks, nParallelism);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::FOpenReadAndCloseCheck(OpenReadAndCloseTask task)
{
    // ContentHandler should intercept fopen() and return a file handle.
    void* stream = ContentHandler::GetInstance().HandleFopen(task.filePath.c_str(), "rb", true);
    ASSERT_NE(stream, nullptr);

    // Fetch the fd for the stream
    int fd = ContentHandler::GetInstance().HandleFileNo(stream);
    ASSERT_NE(fd, -1);

    // Reading the entire file using the file handle should work.
    std::vector<char> readBuf(task.readSize);
    ssize_t nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), task.readSize);
    ASSERT_EQ(nBytesRead, task.readSize);

    // We should not be able to read more data now that we have reached end of file.
    nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), 1);
    ASSERT_EQ(nBytesRead, 0);

    // Closing the file handle should work.
    const int closeRet = ContentHandler::GetInstance().HandleFClose(stream);
    ASSERT_EQ(closeRet, 0);

    // Closing the stream should internally close the fd too and the following fd access should fail.
    nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), 1);
    ASSERT_TRUE(nBytesRead < 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, FOpenReadCloseOneThreadTest)
{
    OpenReadAndCloseTask task;
    task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    task.openFlags = O_RDONLY;
    task.readSize = REMOTE_FILE_SIZE;

    // Execute FOpenReadAndCloseCheck with the task parameters
    FOpenReadAndCloseCheck(task);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, FOpenReadAndCloseManyThreadsTest)
{
    std::vector<OpenReadAndCloseTask> tasks;
    const int nParallelism = 50;
    if (m_bUsingMock)
    {
        for(int idx = 0; idx < nParallelism; idx++)
        {
            OpenReadAndCloseTask task;
            task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
            task.openFlags = O_RDONLY;
            task.readSize = REMOTE_FILE_SIZE;
            tasks.push_back(task);
        }
    }
    else
    {
        std::ifstream testCaseStream(
        StorageInterceptUtils::CombinePath(m_strTestCaseDir, "file_paths_sizes.txt"),
        std::ios::in);
        std::string entry;
        while(std::getline(testCaseStream, entry))
        {
            auto delimiterIndex = entry.rfind(',');
            if (delimiterIndex == std::string::npos)
            {
                continue;
            }
            OpenReadAndCloseTask task;
            task.filePath = entry.substr(0, delimiterIndex);
            task.readSize = static_cast<size_t>(::atoi(entry.substr(delimiterIndex + 1).c_str()));
            task.openFlags = O_RDONLY;
            tasks.push_back(task);
        }
        testCaseStream.close();
    }

    std::function<void(OpenReadAndCloseTask)> lambda =
        std::bind(
            &ContentHandlerTest::FOpenReadAndCloseCheck,
            this,
            std::placeholders::_1);
    ExecuteParallelWithArgs(lambda, tasks, nParallelism);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, FOpenFailureTest)
{
    // Open to non-existing stream should fail
    std::string nonExistingfilePath = m_strInterceptPath + '/' + "some-non-existing-file";
    void* stream = ContentHandler::GetInstance().HandleFopen(nonExistingfilePath.c_str(), "rb", true);
    ASSERT_EQ(stream, nullptr);
    ASSERT_EQ(errno, ENOENT);

    std::string existingFilePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    stream = ContentHandler::GetInstance().HandleFopen(existingFilePath.c_str(), "rb", true);
    ASSERT_NE(stream, nullptr);

    const int closeRet = ContentHandler::GetInstance().HandleFClose(stream);
    ASSERT_EQ(closeRet, 0);

    // We only support opening binary files
    stream = ContentHandler::GetInstance().HandleFopen(existingFilePath.c_str(), "r", true);
    ASSERT_EQ(stream, nullptr);

    // Non-readonly opens should fail
    stream = ContentHandler::GetInstance().HandleFopen(existingFilePath.c_str(), "w", true);
    ASSERT_EQ(stream, nullptr);

    stream = ContentHandler::GetInstance().HandleFopen(existingFilePath.c_str(), "a", true);
    ASSERT_EQ(stream, nullptr);

    stream = ContentHandler::GetInstance().HandleFopen(existingFilePath.c_str(), "r+", true);
    ASSERT_EQ(stream, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandlerTest::OpenAtReadAndCloseCheck(OpenReadAndCloseTask task)
{
    // ContentHandler should intercept openat() and return a file handle.
    int fd = ContentHandler::GetInstance().HandleOpenAt(AT_FDCWD, task.filePath.c_str(), task.openFlags, true, 0);
    ASSERT_TRUE(fd >= 0);

    // Reading the entire file using the file handle should work.
    std::vector<char> readBuf(task.readSize);
    ssize_t nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), task.readSize);
    ASSERT_EQ(nBytesRead, task.readSize);

    // We should not be able to read more data now that we have reached end of file.
    nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), 1);
    ASSERT_EQ(nBytesRead, 0);

    // Closing the file handle should work.
    const int closeRet = ContentHandler::GetInstance().HandleClose(fd);
    ASSERT_EQ(closeRet, 0);

    // we should not be able to read anymore
    nBytesRead = ContentHandler::GetInstance().HandleRead(fd, readBuf.data(), 1);
    ASSERT_TRUE(nBytesRead < 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenAtReadCloseOneThreadTest)
{
    OpenReadAndCloseTask task;
    task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    task.openFlags = O_RDONLY;
    task.readSize = REMOTE_FILE_SIZE;

    // Execute FOpenReadAndCloseCheck with the task parameters
    OpenAtReadAndCloseCheck(task);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenAtReadAndCloseManyThreadsTest)
{
    std::vector<OpenReadAndCloseTask> tasks;
    const int nParallelism = 50;
    if (m_bUsingMock)
    {
        for(int idx = 0; idx < nParallelism; idx++)
        {
            OpenReadAndCloseTask task;
            task.filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
            task.openFlags = O_RDONLY;
            task.readSize = REMOTE_FILE_SIZE;
            tasks.push_back(task);
        }
    }
    else
    {
        std::ifstream testCaseStream(
        StorageInterceptUtils::CombinePath(m_strTestCaseDir, "file_paths_sizes.txt"),
        std::ios::in);
        std::string entry;
        while(std::getline(testCaseStream, entry))
        {
            auto delimiterIndex = entry.rfind(',');
            if (delimiterIndex == std::string::npos)
            {
                continue;
            }
            OpenReadAndCloseTask task;
            task.filePath = entry.substr(0, delimiterIndex);
            task.readSize = static_cast<size_t>(::atoi(entry.substr(delimiterIndex + 1).c_str()));
            task.openFlags = O_RDONLY;
            tasks.push_back(task);
        }
        testCaseStream.close();
    }

    std::function<void(OpenReadAndCloseTask)> lambda =
        std::bind(
            &ContentHandlerTest::OpenAtReadAndCloseCheck,
            this,
            std::placeholders::_1);
    ExecuteParallelWithArgs(lambda, tasks, nParallelism);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenAtFailureTest)
{
    // Open to non-existing stream should fail
    std::string nonExistingfilePath = m_strInterceptPath + '/' + "some-non-existing-file";
    int fd = ContentHandler::GetInstance().HandleOpenAt(AT_FDCWD, nonExistingfilePath.c_str(), O_RDONLY, true, 0);
    ASSERT_EQ(fd, -1);
    ASSERT_EQ(errno, ENOENT);

    // Only read-only access is supported
    std::string existingFilePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    fd = ContentHandler::GetInstance().HandleOpenAt(AT_FDCWD, nonExistingfilePath.c_str(), O_WRONLY, true, 0);
    ASSERT_EQ(fd, -1);

    fd = ContentHandler::GetInstance().HandleOpenAt(AT_FDCWD, nonExistingfilePath.c_str(), O_RDWR, true, 0);
    ASSERT_EQ(fd, -1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, AccessTest)
{
    // Access should succeed for existing files when asked for read-only access
    std::string existingFilePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    int retVal = ContentHandler::GetInstance().HandleAccess(existingFilePath.c_str(), F_OK);
    ASSERT_EQ(retVal, 0);

    retVal = ContentHandler::GetInstance().HandleAccess(existingFilePath.c_str(), R_OK);
    ASSERT_EQ(retVal, 0);

    retVal = ContentHandler::GetInstance().HandleAccess(existingFilePath.c_str(), W_OK);
    ASSERT_EQ(retVal, -1);
    ASSERT_EQ(errno, EACCES);

    retVal = ContentHandler::GetInstance().HandleAccess(existingFilePath.c_str(), W_OK | R_OK);
    ASSERT_EQ(retVal, -1);
    ASSERT_EQ(errno, EACCES);

    std::string nonExistingfilePath = m_strInterceptPath + '/' + "some-non-existing-file";
    retVal = ContentHandler::GetInstance().HandleAccess(nonExistingfilePath.c_str(), F_OK);
    ASSERT_EQ(retVal, -1);
    ASSERT_EQ(errno, ENOENT);

    retVal = ContentHandler::GetInstance().HandleAccess(nonExistingfilePath.c_str(), W_OK);
    ASSERT_EQ(retVal, -1);

    retVal = ContentHandler::GetInstance().HandleAccess(nonExistingfilePath.c_str(), W_OK | R_OK);
    ASSERT_EQ(retVal, -1);
}
//---------------------------------------------------------------------------------------------------------------------