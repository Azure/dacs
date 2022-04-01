///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ContentHandler.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include "StreamingClient.h"
#include "StorageDlsymWrapper.h"
#include "StorageInterceptLogger.h"
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
//---------------------------------------------------------------------------------------------------------------------
ContentHandler::ContentHandler()
    : m_strStorageRoot(StorageInterceptConfig::GetInstance().GetStoragePath())
{
    // Initialize logger
    static LoggingApi _;

    srand(time(NULL));

    LOG_INFO("Initialized ContentHandler. StorageRoot={}", m_strStorageRoot);
}
//---------------------------------------------------------------------------------------------------------------------
ContentHandler& ContentHandler::GetInstance()
{
    static ContentHandler s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleOpen(const char* path, int flags)
{
    return HandleOpen(path, flags, 0777);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleOpen(const char* path, int flags, mode_t mode)
{
    const bool IS_OPEN64 = false;
    return OpenInternal(path, flags, mode, IS_OPEN64);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleOpen64(const char* path, int flags)
{
    return HandleOpen64(path, flags, 0777);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleOpen64(const char* path, int flags, mode_t mode)
{
    const bool IS_OPEN64 = true;
    return OpenInternal(path, flags, mode, IS_OPEN64);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::OpenInternal(const char* path, int flags, mode_t mode, bool isOpen64)
{
    const bool isReadonlyOpen = ((flags & O_RDONLY) == O_RDONLY);

    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, path, relPath);

    if (isRelative && !isReadonlyOpen)
    {
        LOG_INFO(
            "Skipped intercepting non-readonly {} for path={} with flags={}",
            isOpen64 ? "open64" : "open",
            path,
            flags)
    }

    if (!isRelative || !isReadonlyOpen)
    {
        if (!isOpen64)
        {
            return StorageDlsymWrapper::open(path, flags, mode);
        }
        return StorageDlsymWrapper::open64(path, flags, mode);
    }

    FileDescriptor fullFd;
    fullFd.RemotePath = relPath;
    fullFd.OpenFlags = flags;

    // TODO: Do getProperties here in case we need to error out for non-existent files.
    // TODO: Open a directoryStream similar to opendir() once we support open() for directories.
    StreamingClient::GetInstance().OpenFile(relPath);

    int fd = AllotNewFd(fullFd);
    if (fd == -1)
    {
        LOG_ERROR("Could not allot new fd for relative path {}", path);
        return -1;
    }

    LOG_DEBUG(
        "Intercepted the {} system call at path={} flags={} fd={}",
        isOpen64 ? "open64" : "open",
        path,
        flags,
        fd);

    return fd;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandleRead(int fd, void *buf, size_t nBytes)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::read(fd, buf, nBytes);
    }

    const off64_t readOffset = fdIt->second.FileOffset;
    const std::string path = fdIt->second.RemotePath;
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepting the read system call at fd={} path={} nBytes={} readOffset={}",
        fd,
        path,
        nBytes,
        readOffset);

    ssize_t result = StreamingClient::GetInstance().ReadFile(path, buf, nBytes, readOffset);
    if (result < 0)
    {
        LOG_ERROR("StreamingClient read returned error for fd={} path={} nBytes={} readOffset={}",
            fd,
            path,
            nBytes,
            readOffset);
        return result;
    }

    if (result > 0)
    {
        openHandlesLock.lock();
        fdIt = m_mapOpenHandles.find(fd);
        if (fdIt != m_mapOpenHandles.end())
        {
            fdIt->second.FileOffset = (readOffset + result);
        }
        else
        {
            LOG_WARN("File descriptor not found post read(), skipped updating the file offset. fd={} path={}",
                fd,
                path)
        }

        openHandlesLock.unlock();
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandlePRead(int fd, void *buf, size_t nBytes, off64_t offset)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);    
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::pread(fd, buf, nBytes, offset);
    }

    const std::string path = fdIt->second.RemotePath;
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepting the pread system call at fd={} path={} nBytes={} offset={}", fd, path, nBytes, offset);

    ssize_t result = StreamingClient::GetInstance().ReadFile(path, buf, nBytes, offset);
    if (result < 0)
    {
        LOG_ERROR("StreamingClient read returned error for fd={} path={} nBytes={} offset={}",
            fd,
            path,
            nBytes,
            offset);
        return result;
    }

    // Note: Unlike read(), pread() doesn't need to update the file offset post read.
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
off64_t ContentHandler::HandleLseek(int fd, off64_t offset, int whence)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::lseek64(fd, offset, whence);
    }

    off64_t nextOffset = fdIt->second.FileOffset;
    int isModified = 0;
    switch(whence)
    {
        case SEEK_SET:
            isModified = (offset ^ nextOffset);
            nextOffset = offset;
            break;

        case SEEK_CUR:
            isModified |= offset;
            nextOffset += offset;
            break;

        case SEEK_END:
            FileAttributes attributes;
            std::string path = fdIt->second.RemotePath;
            /* offset > 0 is currently not supported for SEEK_END case */
            if (offset > 0)
            {
                StreamingClient::GetInstance().GetProperties(path, attributes);
                LOG_ERROR(
                    "Lseek request to an offset beyond file size encountered. path={}, requestedOffset={}, fileSize={}",
                    path,
                    attributes.size + offset,
                    attributes.size);
                nextOffset = -1;
                return nextOffset;
            }
            /* Releasing lock during expensive I/O operation */
            openHandlesLock.unlock();
            StreamingClient::GetInstance().GetProperties(path, attributes);
            openHandlesLock.lock();
            fdIt = m_mapOpenHandles.find(fd);
            if (fdIt == m_mapOpenHandles.end())
            {
                openHandlesLock.unlock();
                LOG_WARN(
                    "Fd was closed in between lseek handling, falling back to original lseek. path={}, fd={}, offset={}, whence={}",
                    path,
                    fd,
                    offset,
                    whence);
                return StorageDlsymWrapper::lseek64(fd, offset, whence);
            }
            isModified = (attributes.size + offset) ^ nextOffset;
            nextOffset = attributes.size + offset;
            break;

    }
    if (isModified == 0)
    {
        return nextOffset;
    }

    fdIt->second.FileOffset = nextOffset;
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepted the lseek system call and updated offset. fd={} offset={} whence={} nextOffset={}",
        fd,
        offset,
        whence,
        nextOffset);
    return nextOffset;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleClose(int fd)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::close(fd);
    }

    FileDescriptor fullFd = fdIt->second;
    m_mapOpenHandles.erase(fd);
    openHandlesLock.unlock();

    StreamingClient::GetInstance().Close(fullFd.RemotePath);

    LOG_DEBUG("Intercepted the close system call at fd={} path={}", fd, fullFd.RemotePath);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void* ContentHandler::HandleOpendir(const char* path)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, path, relPath);

    if (!isRelative)
    {
        return StorageDlsymWrapper::opendir(path);
    }

    LOG_DEBUG("Intercepting the opendir system call for {}", path);

    std::unique_ptr<DirectoryStream> dirStream = std::make_unique<DirectoryStream>();
    dirStream->RemotePath = relPath;

    DirectoryStream* dirStreamPtr = dirStream.get();

    // Add "." and ".." entries manually to the directory stream.
    dirStreamPtr->Entries.push_back(DirectoryEntry());
    dirStreamPtr->Entries.back().d_ino = D_INO_VAL;
    dirStreamPtr->Entries.back().d_off = 0;
    dirStreamPtr->Entries.back().d_reclen = sizeof(DirectoryEntry);
    dirStreamPtr->Entries.back().d_type = DT_DIR;
    strncpy(dirStreamPtr->Entries.back().d_name, ".", D_NAME_LEN);

    dirStreamPtr->Entries.push_back(DirectoryEntry());
    dirStreamPtr->Entries.back().d_ino = D_INO_VAL;
    dirStreamPtr->Entries.back().d_off = 1;
    dirStreamPtr->Entries.back().d_reclen = sizeof(DirectoryEntry);
    dirStreamPtr->Entries.back().d_type = DT_DIR;
    strncpy(dirStreamPtr->Entries.back().d_name, "..", D_NAME_LEN);

    std::unique_lock<std::mutex> dirStreamLock(m_lockDirectoryStreams);
    m_mapDirectoryStreams[dirStreamPtr] = std::move(dirStream);

    return dirStreamPtr;
}
//---------------------------------------------------------------------------------------------------------------------
void* ContentHandler::HandleReaddir64(void* dirp)
{
    std::unique_lock<std::mutex> dirStreamLock(m_lockDirectoryStreams);
    if (m_mapDirectoryStreams.find(static_cast<DirectoryStream *>(dirp)) == m_mapDirectoryStreams.end())
    {
        return StorageDlsymWrapper::readdir64(dirp);
    }
    dirStreamLock.unlock();

    DirectoryStream& dirStream = *(static_cast<DirectoryStream *>(dirp));
    LOG_DEBUG("Intercepting the readdir system call for {}", dirStream.RemotePath);

    if (static_cast<size_t>(dirStream.Offset) < dirStream.Entries.size())
    {
        // Entry available to read
        DirectoryEntry* ret = &(dirStream.Entries[dirStream.Offset]);
        dirStream.Offset++;
        return ret;
    }
    else if (!dirStream.AllEntriesFetched)
    {
        // Entry not yet fetched from backend.
        std::string newContinuationToken;
        newContinuationToken = StreamingClient::GetInstance().ReadDirectory(
            dirStream.RemotePath,
            dirStream.ContinuationToken,
            dirStream.Entries);

        dirStream.ContinuationToken = newContinuationToken;
        if (newContinuationToken == "")
        {
            dirStream.AllEntriesFetched = true;
        }

        // Retry reading with newly fetched directory entries.
        return HandleReaddir64(dirp);
    }
    else
    {
        // No more entries.
        return NULL;
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleClosedir(void* dirp)
{
    std::unique_lock<std::mutex> dirStreamLock(m_lockDirectoryStreams);
    if (m_mapDirectoryStreams.find(static_cast<DirectoryStream *>(dirp)) == m_mapDirectoryStreams.end())
    {
        return StorageDlsymWrapper::closedir(dirp);
    }

    LOG_DEBUG("Intercepting the closedir system call for {}", (((DirectoryStream *)dirp)->RemotePath));
    m_mapDirectoryStreams.erase((DirectoryStream *)dirp);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleXstat(int __ver, const char* pathname, struct stat* statbuf)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, pathname, relPath);

    if (!isRelative)
    {
        return StorageDlsymWrapper::xstat(__ver, pathname, statbuf);
    }

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(relPath, "xstat", &stat64buf);
    if (result == 0)
    {
        CopyStat64ToStat(&stat64buf, statbuf);
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleXstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, pathname, relPath);

    if (!isRelative)
    {
        return StorageDlsymWrapper::xstat64(__ver, pathname, statbuf);
    }

    return GetPropertiesInternal(relPath, "xstat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFxstat(int __ver, int fd, struct stat* statbuf)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::fxstat(__ver, fd, statbuf);
    }
    const std::string path = fdIt->second.RemotePath;
    openHandlesLock.unlock();

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(path, "fxstat", &stat64buf);
    if (result == 0)
    {
        CopyStat64ToStat(&stat64buf, statbuf);
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFxstat64(int __ver, int fd, struct stat64* statbuf)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::fxstat64(__ver, fd, statbuf);
    }
    const std::string path = fdIt->second.RemotePath;
    openHandlesLock.unlock();

    return GetPropertiesInternal(path, "fxstat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleLxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, pathname, relPath);

    if (!isRelative)
    {
        return StorageDlsymWrapper::lxstat(__ver, pathname, statbuf);
    }

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(relPath, "lxstat", &stat64buf);
    if (result == 0)
    {
        CopyStat64ToStat(&stat64buf, statbuf);
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleLxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, pathname, relPath);

    if (!isRelative)
    {
        return StorageDlsymWrapper::lxstat64(__ver, pathname, statbuf);
    }

    return GetPropertiesInternal(relPath, "lxstat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::GetPropertiesInternal(
    const std::string& relPath,
    const char* interceptedCall,
    struct stat64* statbuf)
{
    LOG_DEBUG("Intercepting {} call for remote path {}", interceptedCall, relPath);

    FileAttributes attributes;
    int result = StreamingClient::GetInstance().GetProperties(relPath, attributes);
    if (result < 0)
    {
        LOG_ERROR("Error handling {} call for remote path {}", interceptedCall, relPath);
        result = -1;
        return result;
    }

    PopulateStatBuf(attributes, statbuf);
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleIoctl(int fd, unsigned long request, void* argp)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::ioctl(fd, request, argp);
    }

    LOG_DEBUG("Intercepting the ioctl call for fd={} request={} path={}", fd, request, fdIt->second.RemotePath);

    // Note: ioctl() is called by python's pathlib module during file open with request as FIOCLEX, which is a no-op
    //       for us.
    // TODO: see if there are any other use-cases which call ioctl() and handle accordingly.
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleIsatty(int fd)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::isatty(fd);
    }

    LOG_DEBUG("Intercepting the isatty call for fd={} path={}", fd, fdIt->second.RemotePath);

    // isatty() is supposed to return 1 if fd refers to a terminal and 0 otherwise (which is the case here).
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFlock(int fd, int operation)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageDlsymWrapper::flock(fd, operation);
    }

    LOG_DEBUG(
        "Intercepting the flock call for fd={} path={} operation={} LOCK_SH={} LOCK_EX={} LOCK_NB={}",
        fd,
        fdIt->second.RemotePath,
        operation,
        (operation & LOCK_SH),
        (operation & LOCK_EX),
        (operation & LOCK_NB));

    // Note: Only use case we have encountered for flock() is taking shared locks on a file before reading it.
    //       We would need to implement locking here if we encounter use cases where an exclusive lock is taken.
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::AllotNewFd(const FileDescriptor& fd)
{
    const uint8_t maxAttempts = 5;
    uint8_t nAttempts = 0;

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    while(nAttempts < maxAttempts)
    {
        int newFd = rand() % 100000 + 90000; // 90000 is added to avoid collision with system alloted fds.
        if (m_mapOpenHandles.find(newFd) == m_mapOpenHandles.end())
        {
            m_mapOpenHandles[newFd] = fd;
            return newFd;
        }

        nAttempts++;
    }

    LOG_ERROR("Could not get new fd after {} attempts", nAttempts);
    return -1;
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::PopulateStatBuf(const FileAttributes& stats, struct stat64* statbuf)
{
    static const int DEFAULT_PERMISSION = 0777;
    statbuf->st_mode = 0;
    if (stats.isDir)
    {
        statbuf->st_mode |= S_IFDIR;
    }
    else
    {
        statbuf->st_mode |= S_IFREG;
    }

    statbuf->st_mode |= DEFAULT_PERMISSION;
    statbuf->st_size = stats.size;
    statbuf->st_mtime = stats.mTime.tv_sec;
    statbuf->st_atime = stats.aTime.tv_sec;
    statbuf->st_ctime = stats.cTime.tv_sec;
    statbuf->st_blksize = StorageInterceptConfig::GetInstance().GetIoBlockSize();
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::CopyStat64ToStat(const struct stat64* stat64buf, struct stat* statbuf)
{
    statbuf->st_mode = stat64buf->st_mode;
    statbuf->st_size = stat64buf->st_size;
    statbuf->st_mtime = stat64buf->st_mtime;
    statbuf->st_atime = stat64buf->st_atime;
    statbuf->st_ctime = stat64buf->st_ctime;
    statbuf->st_blksize = stat64buf->st_blksize;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandleListxattr(const char* path, char* list, size_t size)
{
    std::string relPath;
    const bool isRelative = StorageInterceptUtils::GetRelativePath(m_strStorageRoot, path, relPath);

    if (isRelative)
    {
        /* Listxattr and other extended attributes related calls' interception is currently not implemented */
        LOG_INFO(
            "Listxattr called at path: {} is not intercepted, falling back to it's original implementation",
            path);
    }
    return StorageDlsymWrapper::listxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------
