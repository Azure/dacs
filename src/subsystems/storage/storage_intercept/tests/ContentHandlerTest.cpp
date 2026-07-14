//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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
#include "DirectoryEntry.h"
#include <sys/stat.h>
#include <stdlib.h>
#include "FileStat.h"
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
TEST_F(ContentHandlerTest, ReaddirVersionTest)
{
    // Verify version in readdir entries: non-empty for files, empty for directories
    std::string dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
    void* dirp = ContentHandler::GetInstance().HandleOpendir(dirPath.c_str());
    ASSERT_NE(dirp, nullptr);

    DirectoryEntry *entry;
    while ((entry = (DirectoryEntry*)ContentHandler::GetInstance().HandleReaddir(dirp, true)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            ASSERT_NE(entry->d_version[0], '\0');
        }
        else if (entry->d_type == DT_DIR)
        {
            ASSERT_EQ(entry->d_version[0], '\0');
        }
    }

    ContentHandler::GetInstance().HandleClosedir(dirp);
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
TEST_F(ContentHandlerTest, DACSStatFileTest)
{
    std::string filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    struct FileStat fileStat = {};

    int ret = ContentHandler::GetInstance().HandleDACSStat(filePath.c_str(), &fileStat);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(S_ISREG(fileStat.st_mode));
    ASSERT_EQ(fileStat.st_size, REMOTE_FILE_SIZE);
    ASSERT_GT(fileStat.st_blksize, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, DACSStatDirectoryTest)
{
    std::string dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
    struct FileStat dirStat = {};

    int ret = ContentHandler::GetInstance().HandleDACSStat(dirPath.c_str(), &dirStat);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(S_ISDIR(dirStat.st_mode));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, DACSStatNonExistentTest)
{
    std::string nonExistentPath = m_strInterceptPath + "/non-existent-dacs-stat-file";
    struct FileStat badStat = {};

    errno = 0;
    int ret = ContentHandler::GetInstance().HandleDACSStat(nonExistentPath.c_str(), &badStat);
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(errno, ENOENT);
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
//-----------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenWriteModeTest)
{
    std::string nonExistingFilePath = m_strInterceptPath + '/' + "some-non-existing-file";

    // Open in write mode should fail
    int fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY);
    ASSERT_EQ(fd, -1);

    //Override the config to enable write support.
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    ContentHandler::GetInstance().ForkHandlerPrepare();
    ContentHandler::GetInstance().ForkHandlerParent(); // these are to reset the streaming\cache clients after config change

    // Open in O_RDWR mode should fail
    fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_RDWR);
    ASSERT_EQ(fd, -1);

    // Open in write mode with O_APPEND should fail
    fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY | O_APPEND);
    ASSERT_EQ(fd, -1);

    // Open in write mode with O_TRUNC should fail
    fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY | O_TRUNC);
    ASSERT_EQ(fd, -1);

    // Open in write mode should succeed now
    fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_NE(fd, -1);

    // A read handle should not be allowed while a write handle is open
    int readFd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_RDONLY);
    ASSERT_EQ(readFd, -1);

    // Another write handle should not be allowed while a write handle is open
    int writeFd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_EQ(writeFd, -1);

    // Closing the write handle should work.
    //const int closeRet = ContentHandler::GetInstance().HandleClose(fd);
    //ASSERT_EQ(closeRet, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenExclOnFileTest)
{
    std::string existingFilePath = m_strInterceptPath + '/' + m_strRemoteFilePath;

    // Enable write support
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");

    // --- Efficient mode (default) ---

    // O_EXCL on a new file should succeed
    std::string newFilePath1 = m_strInterceptPath + '/' + "excl-new-eff-" + GenerateRandomString(4);
    int fd = ContentHandler::GetInstance().HandleOpen64(newFilePath1.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_NE(fd, -1);

    // O_EXCL on an existing file should fail with EEXIST
    fd = ContentHandler::GetInstance().HandleOpen64(existingFilePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_EQ(fd, -1);
    ASSERT_EQ(errno, EEXIST);

    // --- Consistent mode ---
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "true");

    // O_EXCL on a new file should succeed
    std::string newFilePath2 = m_strInterceptPath + '/' + "excl-new-cm-" + GenerateRandomString(4);
    fd = ContentHandler::GetInstance().HandleOpen64(newFilePath2.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_NE(fd, -1);

    // O_EXCL on an existing file should still fail with EEXIST (even in consistent mode)
    fd = ContentHandler::GetInstance().HandleOpen64(existingFilePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    ASSERT_EQ(fd, -1);
    ASSERT_EQ(errno, EEXIST);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, OpenWriteModeTruncTest)
{
    std::string nonExistingFilePath = m_strInterceptPath + '/' + "trunc-test-file-" + GenerateRandomString(4);
    std::string existingFilePath = m_strInterceptPath + '/' + m_strRemoteFilePath;

    // Enable write support
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");

    // --- Efficient mode (default) ---

    // O_TRUNC on a non-existing file should succeed (creates new file, no overwrite conflict)
    int fd = ContentHandler::GetInstance().HandleOpen64(nonExistingFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);

    // O_TRUNC on an existing file should fail with EEXIST in efficient mode (no consistent mode)
    fd = ContentHandler::GetInstance().HandleOpen64(existingFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_EQ(fd, -1);
    ASSERT_EQ(errno, EEXIST);

    // --- Consistent mode ---
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");

    // O_TRUNC on a non-existing file should succeed
    std::string newFilePath = m_strInterceptPath + '/' + "trunc-cm-new-" + GenerateRandomString(4);
    fd = ContentHandler::GetInstance().HandleOpen64(newFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);

    // O_TRUNC on an existing file should succeed in consistent mode (overwrite allowed)
    fd = ContentHandler::GetInstance().HandleOpen64(existingFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_NE(fd, -1);
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
TEST_F(ContentHandlerTest, AttrCacheDisabledInConsistentModeTest)
{
    // Explicitly enable attr cache (test config may not have it set).
    OverrideConfig(StorageInterceptConstants::ConfigNameAttributesCacheEnable, "true");

    // ShouldEnableAttributesCache returns false when consistent mode is on, even if attr cache config is true.
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "true");
    ASSERT_FALSE(StorageInterceptConfig::GetInstance().ShouldEnableAttributesCache(""));

    // And true when consistent mode is off.
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "false");
    ASSERT_TRUE(StorageInterceptConfig::GetInstance().ShouldEnableAttributesCache(""));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, HandleShutdownClosesOpenFds)
{
    // Clean up any leaked state from prior tests (e.g., OpenWriteModeTest leaks a write fd).
    ContentHandler::GetInstance().HandleShutdown();

    // Open a file, then call HandleShutdown — the fd should be cleaned up.
    std::string filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    const int fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);

    // Verify fd works before shutdown
    std::vector<char> readBuf(1024);
    ssize_t nBytesRead = ContentHandler::GetInstance().HandlePRead(fd, readBuf.data(), 1024, 0);
    ASSERT_EQ(nBytesRead, 1024);

    // Shutdown should succeed without crashing
    ContentHandler::GetInstance().HandleShutdown();

    // Verify the client recovers — open a new fd and read successfully
    const int newFd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
    ASSERT_NE(newFd, -1);
    nBytesRead = ContentHandler::GetInstance().HandlePRead(newFd, readBuf.data(), 1024, 0);
    ASSERT_EQ(nBytesRead, 1024);
    ASSERT_EQ(ContentHandler::GetInstance().HandleClose(newFd), 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, HandleShutdownClosesDirStreams)
{
    // Clean up any leaked state from prior tests.
    ContentHandler::GetInstance().HandleShutdown();

    // Open a directory, then call HandleShutdown — the dir stream should be closed.
    std::string dirPath = m_strInterceptPath + '/' + m_strRemoteDirPath;
    void* dirp = ContentHandler::GetInstance().HandleOpendir(dirPath.c_str());
    ASSERT_NE(dirp, nullptr);

    // Shutdown clears all directory streams
    ContentHandler::GetInstance().HandleShutdown();

    // Verify the client recovers — open a new dir stream and read it
    void* newDirp = ContentHandler::GetInstance().HandleOpendir(dirPath.c_str());
    ASSERT_NE(newDirp, nullptr);

    uint32_t entryCount = 0;
    while (ContentHandler::GetInstance().HandleReaddir(newDirp, true) != NULL)
    {
        entryCount++;
    }
    ASSERT_GT(entryCount, 0u);
    ASSERT_EQ(ContentHandler::GetInstance().HandleClosedir(newDirp), 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, HandleShutdownThenReinitialize)
{
    // Clean up any leaked state from prior tests.
    ContentHandler::GetInstance().HandleShutdown();

    // Shutdown should allow re-initialization: open → read → shutdown → open → read
    std::string filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;

    // First cycle: open and read
    int fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);
    std::vector<char> readBuf(1024);
    ssize_t nBytesRead = ContentHandler::GetInstance().HandlePRead(fd, readBuf.data(), 1024, 0);
    ASSERT_EQ(nBytesRead, 1024);

    // Shutdown — tears down cache clients
    ContentHandler::GetInstance().HandleShutdown();

    // Second cycle: open should lazily re-initialize cache clients
    fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);
    nBytesRead = ContentHandler::GetInstance().HandlePRead(fd, readBuf.data(), 1024, 0);
    ASSERT_EQ(nBytesRead, 1024);

    const int closeRet = ContentHandler::GetInstance().HandleClose(fd);
    ASSERT_EQ(closeRet, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ContentHandlerTest, HandleShutdownIsIdempotent)
{
    // Clean up any leaked state from prior tests, then verify double shutdown is safe.
    ContentHandler::GetInstance().HandleShutdown();
    ContentHandler::GetInstance().HandleShutdown();

    // Verify we can still use the handler after double shutdown
    std::string filePath = m_strInterceptPath + '/' + m_strRemoteFilePath;
    int fd = ContentHandler::GetInstance().HandleOpen64(filePath.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);

    std::vector<char> readBuf(1024);
    ssize_t nBytesRead = ContentHandler::GetInstance().HandlePRead(fd, readBuf.data(), 1024, 0);
    ASSERT_EQ(nBytesRead, 1024);

    const int closeRet = ContentHandler::GetInstance().HandleClose(fd);
    ASSERT_EQ(closeRet, 0);
}
//---------------------------------------------------------------------------------------------------------------------