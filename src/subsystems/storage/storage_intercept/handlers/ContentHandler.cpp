///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ContentHandler.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include "StreamingClient.h"
#include "StorageLibcApiWrapper.h"
#include "StorageInterceptLogger.h"
#include "LocalFSCacheClient.h"
#include "RemoteCacheClient.h"
#include "LocalAttributesCache.h"
#include "InMemoryAttributesCache.h"
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
//---------------------------------------------------------------------------------------------------------------------
/* Initialize logging during shared object init. This is to avoid cyclic dependencies during logging initialization 
 * when log init is invoked during first log api call.
 */
__attribute__((constructor)) void LoggingInit(void) {
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();
}
//---------------------------------------------------------------------------------------------------------------------
thread_local std::string ContentHandler::m_threadLocalLastError;
//---------------------------------------------------------------------------------------------------------------------
ContentHandler::ContentHandler()
  : m_bForkHandlersSetup(false)
  , m_spCacheClient(nullptr)
  , m_spAttributesCache(nullptr)
  , m_bCacheClientsInitialized(false)
{
    // Register handlers to handle process fork
    if (pthread_atfork(ContentHandler::ForkHandlerPrepare,
        ContentHandler::ForkHandlerParent,
        ContentHandler::ForkHandlerChild) == 0)
    {
        m_bForkHandlersSetup = true;
    }
    else
    {
        m_bForkHandlersSetup = false;
    }

    // There should be no logging from this constructor to prevent deadlock with LFS interceptor.
    LoadMountInfoConfig();
    srand(time(NULL));
}
//---------------------------------------------------------------------------------------------------------------------
ContentHandler& ContentHandler::GetInstance()
{
    static ContentHandler s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::LoadMountInfoConfig()
{
    if (StorageInterceptConfig::GetInstance().GetMultipleMountEnabled())
    {
        std::vector<std::string> mountNames = StorageInterceptConfig::GetInstance().GetMountNames();
        for (const std::string& mountName : mountNames)
        {
            if (mountName != "")
            {
                std::string mountPath = StorageInterceptConfig::GetInstance().GetStoragePath(mountName);
                m_vecMountInfo.push_back(std::make_unique<MountInfo>(mountName, mountPath));
            }
        }

        if (m_vecMountInfo.size() == 0)
        {
            throw "There must be at least one mount specified";
        }

        // TODO: Validate that no mount path is a subdirectory of other
    }
    else
    {
        m_vecMountInfo.push_back(std::make_unique<MountInfo>(
            "" /* mountName */,
            StorageInterceptConfig::GetInstance().GetStoragePath("")));
    }
}
//---------------------------------------------------------------------------------------------------------------------
ContentHandler::MountInfo* ContentHandler::CheckInterceptionForPath(
    const char* path,
    std::string& outRelativePath)
{
#ifdef STORAGE_INTERCEPT_FUSE
    const bool m_bIsFuseMount = true;
#else
    const bool m_bIsFuseMount = false;
#endif

    for (const std::unique_ptr<MountInfo>& mountInfo : m_vecMountInfo)
    {
        if (m_bIsFuseMount)
        {
            outRelativePath = "";
            int pathLen = strlen(path);
            int pathIdx = 0;
            const char SEPARATOR_CHAR = '/';

            if (pathLen == 0) {
                return mountInfo.get();
            }

            // Don't include starting separator in relative path
            while (pathIdx < pathLen && path[pathIdx] == SEPARATOR_CHAR) pathIdx++;

            // Append rest of path to relative path (ignoring consecutive separators)
            while (pathIdx < pathLen) {
                outRelativePath += path[pathIdx];

                while (path[pathIdx] == SEPARATOR_CHAR && pathIdx + 1 < pathLen && path[pathIdx+1] == SEPARATOR_CHAR) pathIdx++;
                pathIdx++;
            }

            return mountInfo.get();
        }

        const bool isRelative = StorageInterceptUtils::GetRelativePath(mountInfo->MountPath, path, outRelativePath);
        if (isRelative) return mountInfo.get();
    }

    return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------
ContentHandler::MountInfo& ContentHandler::GetMountInfo(const std::string& mountName)
{
    for (const std::unique_ptr<MountInfo>& mountInfo : m_vecMountInfo)
    {
        if (mountName == mountInfo->MountName) return *mountInfo;
    }

    LOG_ERROR("MountInfo not found for mountName={}", mountName);
    throw "MountInfo not found";
}
//---------------------------------------------------------------------------------------------------------------------
StreamingClient& ContentHandler::GetStreamingClient(MountInfo& mountInfo)
{
    std::unique_lock<std::mutex> lock(mountInfo.LockStreamingClient);
    if (mountInfo.StreamingClientPtr == nullptr)
    {
        std::unique_lock<std::mutex> cacheClientsLock(m_lockCacheClients);
        if (!m_bCacheClientsInitialized)
        {
            LOG_INFO("Initializing cache clients for first time in ContentHandler")
            SetCacheClients();
        }

        mountInfo.StreamingClientPtr = std::make_unique<StreamingClient>(
            mountInfo.MountName,
            std::atomic_load(&m_spCacheClient),
            std::atomic_load(&m_spAttributesCache));
    }

    return *(mountInfo.StreamingClientPtr);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::ForkHandlerPrepare()
{
    GetInstance().ClearCacheClients();
    for (auto& mountInfo : GetInstance().m_vecMountInfo)
    {
        if (mountInfo->StreamingClientPtr != nullptr)
        {
            mountInfo->StreamingClientPtr->ForkHandlerPrepare();
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::ForkHandlerParent()
{
    std::unique_lock<std::mutex> cacheClientsLock(GetInstance().m_lockCacheClients);
    if (!GetInstance().m_bCacheClientsInitialized) return;

    LOG_INFO("Initializing cache clients for parent after fork")
    GetInstance().SetCacheClients();
    for (auto& mountInfo : GetInstance().m_vecMountInfo)
    {
        if (mountInfo->StreamingClientPtr != nullptr)
        {
            mountInfo->StreamingClientPtr->ForkHandlerParent(
                std::atomic_load(&GetInstance().m_spCacheClient),
                std::atomic_load(&GetInstance().m_spAttributesCache));
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::ForkHandlerChild()
{
    std::unique_lock<std::mutex> cacheClientsLock(GetInstance().m_lockCacheClients);
    if (!GetInstance().m_bCacheClientsInitialized) return;

    LOG_INFO("Initializing cache clients for child after fork")
    GetInstance().SetCacheClients();
    for (auto& mountInfo : GetInstance().m_vecMountInfo)
    {
        if (mountInfo->StreamingClientPtr != nullptr)
        {
            mountInfo->StreamingClientPtr->ForkHandlerChild(
                std::atomic_load(&GetInstance().m_spCacheClient),
                std::atomic_load(&GetInstance().m_spAttributesCache));
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::ClearCacheClients()
{
    const std::shared_ptr<ICacheClient> emptyCacheClient;
    std::atomic_store(&m_spCacheClient, emptyCacheClient);

    const std::shared_ptr<IAttributesCache> emptyAttributesCache;
    std::atomic_store(&m_spAttributesCache, emptyAttributesCache);
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::SetCacheClients()
{
    // Check if data cache enabled for at least one mount.
    bool dataCacheEnabled = false;
    for (const auto& mountInfo : m_vecMountInfo)
    {
        if (StorageInterceptConfig::GetInstance().GetCacheEnable(mountInfo->MountName))
        {
            dataCacheEnabled = true;
            break;
        }
    }

    std::shared_ptr<RemoteCacheClient> remoteCacheClient;
    if (dataCacheEnabled)
    {
        if(StorageInterceptConfig::GetInstance().GetCacheUseLocalFS())
        {
            std::atomic_store(&m_spCacheClient, std::shared_ptr<ICacheClient>(std::make_shared<LocalFSCacheClient>()));
            LOG_INFO("Initialized LocalFS cache client in ContentHandler")
        }
        else
        {
            remoteCacheClient = std::make_shared<RemoteCacheClient>(StorageInterceptConfig::GetInstance().GetStreamingChunkSize());
            std::atomic_store(&m_spCacheClient, std::shared_ptr<ICacheClient>(remoteCacheClient));
            LOG_INFO("Initialized remote cache client in ContentHandler")
        }
    }
    else
    {
        const std::shared_ptr<ICacheClient> emptyCacheClient;
        std::atomic_store(&m_spCacheClient, emptyCacheClient);
        LOG_INFO("Initialized empty cache client in ContentHandler")
    }

    // Check if attr cache enabled for at least one mount.
    bool attrCacheEnabled = false;
    for (const auto& mountInfo : m_vecMountInfo)
    {
        if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable(mountInfo->MountName))
        {
            attrCacheEnabled = true;
            break;
        }
    }

    if (attrCacheEnabled)
    {
        if (StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
        {
            std::atomic_store(
                &m_spAttributesCache,
                std::shared_ptr<IAttributesCache>(std::make_shared<LocalAttributesCache>()));
            LOG_INFO("Initialized Local server attributes cache client in ContentHandler")
        }
        else if (StorageInterceptConfig::GetInstance().GetAttributesCacheUseRemoteServer())
        {
            // reuse RemoteCacheClient if its already instantiated
            if (!remoteCacheClient.get())
            {
                remoteCacheClient = std::make_shared<RemoteCacheClient>(StorageInterceptConfig::GetInstance().GetStreamingChunkSize());
            }

            std::atomic_store(
                &m_spAttributesCache,
                std::shared_ptr<IAttributesCache>(remoteCacheClient));
            LOG_INFO("Initialized Remote attributes cache client in ContentHandler")
        }
        else
        {
            std::atomic_store(
                &m_spAttributesCache,
                std::shared_ptr<IAttributesCache>(std::make_shared<InMemoryAttributesCache>()));
            LOG_INFO("Initialized InMemory attributes cache client in ContentHandler")
        }
    }
    else
    {
        const std::shared_ptr<IAttributesCache> emptyAttributesCache;
        std::atomic_store(&m_spAttributesCache, emptyAttributesCache);
        LOG_INFO("Initialized empty attributes cache client in ContentHandler")
    }

    m_bCacheClientsInitialized = true;
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
void* ContentHandler::HandleFopen(const char* path, const char* mode, bool isOpen64)
{
    // Check for binary and readonly flags "rb"
    // NOTE: Write modes support is not added for fopen\fopen64, but is for open\open64 as python package uses it.
    const bool isReadonlyOpen = (strcmp(mode, "rb") == 0);
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(path, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (isRelative && !isReadonlyOpen)
    {
        LOG_WARN(
            "Skipped intercepting non-readonly {} for path={} with mode={}",
            isOpen64 ? "fopen64" : "fopen",
            path,
            mode)
    }

    if (!isRelative || !isReadonlyOpen)
    {
        if (!isOpen64)
        {
            return StorageLibcApiWrapper::fopen(path, mode);
        }
        return StorageLibcApiWrapper::fopen64(path, mode);
    }

    std::unique_ptr<FILE> fileStream = std::make_unique<FILE>();
    void* fileStreamPtr = fileStream.get();
    memset(fileStreamPtr, 0, sizeof(FILE));

    int fd = OpenValidatedPathInternal(*mountInfo, relPath, O_RDONLY, std::move(fileStream));
    LOG_DEBUG(
        "Intercepted the {} system call at path={} mountName={} mode={} fd={}",
        isOpen64 ? "fopen64" : "fopen",
        path,
        mountInfo->MountName,
        mode,
        fd);

    if (fd < 0)
    {
        LOG_WARN("Failed to open and alloc fd for mountName={} path={}", mountInfo->MountName, path);
        return nullptr;
    }

    return fileStreamPtr;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFileNo(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fileno(stream);
    }

    LOG_DEBUG("Intercepted the fileno system call fd={}", fd);
    return fd;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFClose(void* stream)
{
    // Extract the fd and check if we manage this, if not fallback to libc wrapper
    // If stream is valid and _fileno is set to -1, we know that this is managed by us and extract the fd from _flags.
    FILE* fileStreamPtr = static_cast<FILE*>(stream);
    if (fileStreamPtr == nullptr || fileStreamPtr->_fileno != -1 || !IsFdInRange(fileStreamPtr->_flags))
    {
        return StorageLibcApiWrapper::fclose(stream);
    }

    int fd = fileStreamPtr->_flags;
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::fclose(stream);
    }

    const std::string remotePath = fdIt->second->RemotePath;
    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    m_mapOpenHandles.erase(fdIt);
    openHandlesLock.unlock();

    GetStreamingClient(mountInfo).Close(remotePath, false /* isWriteMode */);

    LOG_DEBUG("Intercepted the fclose system call at fd={} path={} mountName={}",
        fd,
        remotePath,
        mountInfo.MountName);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleOpenAt(int dirfd, const char* path, int flags, mode_t mode, bool isOpen64)
{

    // If dirfd is invalid or AT_FDCWD (path relative to current working dir) or if path is an absolute path, 
    // this should be treated same as regular open.
    if (dirfd < 0 || dirfd == AT_FDCWD || (path != nullptr && path[0] == '/'))
    {
        std::string relPath;
        MountInfo* mountInfo = CheckInterceptionForPath(path, relPath);
        const bool isRelative = (mountInfo != nullptr);

        if (!isRelative)
        {
            if (!isOpen64)
            {
                return StorageLibcApiWrapper::openat(dirfd, path, flags, mode);
            }
            return StorageLibcApiWrapper::openat64(dirfd, path, flags, mode);
        }

        LOG_DEBUG(
            "Intercepted the {} system call at mountName={} path={} dirfd={} flags={}",
            isOpen64 ? "openat64" : "openat",
            mountInfo->MountName,
            path,
            dirfd,
            flags);
        return OpenValidatedPathInternal(*mountInfo, relPath, flags);
    }
    else
    {        
        if (!IsFdInRange(dirfd))
        {
            if (!isOpen64)
            {
                return StorageLibcApiWrapper::openat(dirfd, path, flags, mode);
            }
            return StorageLibcApiWrapper::openat64(dirfd, path, flags, mode);
        }

        // Fallback to system wrapper if we are not managing this fd
        std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
        auto fdIt = m_mapOpenHandles.find(dirfd);
        if (fdIt == m_mapOpenHandles.end())
        {
            openHandlesLock.unlock();
            if (!isOpen64)
            {
                return StorageLibcApiWrapper::openat(dirfd, path, flags, mode);
            }
            return StorageLibcApiWrapper::openat64(dirfd, path, flags, mode);
        }

        // path is relative to the directory referred to by dirfd.
        // TODO: Support for this is not yet implemented.
        LOG_ERROR("openat with valid dirfd not supported, path={} dirfd={} dirpath={}",
            path, dirfd, fdIt->second->RemotePath);
        throw "openat with valid dirfd not supported";
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::OpenValidatedPathInternal(
    MountInfo& mountInfo,
    const std::string& relativePath,
    int flags,
    std::unique_ptr<FILE> fileStream)
{
    // Validate the flags.
    if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_WRONLY)
    {
        LOG_ERROR("Only O_RDONLY and O_WRONLY access modes are supported for open, "
            "mountName={} path={} flags={}",
            mountInfo.MountName,
            relativePath,
            flags);
        return -1;
    }
    if ((flags & O_WRONLY) && (flags != (O_WRONLY | O_CREAT | O_EXCL)))
    {
        LOG_ERROR("Only (O_WRONLY | O_CREAT | O_EXCL) flags are supported for O_WRONLY open, "
            "mountName={} path={} flags={}",
            mountInfo.MountName,
            relativePath,
            flags);
        return -1;
    }

    std::unique_ptr<FileDescriptor> fullFd = std::make_unique<FileDescriptor>();
    fullFd->MountName = mountInfo.MountName;
    fullFd->RemotePath = relativePath;
    fullFd->OpenFlags = flags;
    fullFd->FileStream = move(fileStream);
    const bool isWriteMode = ((flags & O_ACCMODE) == O_WRONLY);
    fullFd->IsWriteMode = isWriteMode;

    if (fullFd->IsWriteMode)
    {
        if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
        {
            LOG_ERROR("Write support is not enabled, mountName={} path={}", mountInfo.MountName, relativePath);
            return -1;
        }

        uint32_t openFileHandleCount = GetOpenFileHandleCount(relativePath);
        if (openFileHandleCount > 0)
        {
            LOG_ERROR("Client already has {} open file descriptor(s). Cannot open another for writing."
                " mountName={} path={}",
                openFileHandleCount,
                mountInfo.MountName,
                relativePath);
            return -1;
        }
    }
    else
    {
        if (IsOpenedForWrite(relativePath))
        {
            LOG_ERROR("Client already has another open file descriptor for writing, cannot read/write to the same "
                "file at once. mountName={} path={}",
                mountInfo.MountName, relativePath);
            return -1;
        }
    }

    int getPropertiesResult = GetStreamingClient(mountInfo).GetProperties(relativePath, fullFd->Attributes);
    if (getPropertiesResult < 0)
    {
        // GetProperties is allowed to fail with ENOENT only if the open is in write mode with O_CREAT flag.
        if (errno == ENOENT && isWriteMode && (flags & O_CREAT) != 0)
        {
            LOG_INFO("Intercepted a write open for a non-existent file, creating a new file mountName={} path={}",
                mountInfo.MountName,
                relativePath);
            fullFd->Attributes.size = 0;
            fullFd->Attributes.isDir = false;
            struct timespec curTimeSpec;
            clock_gettime(CLOCK_REALTIME, &curTimeSpec);
            fullFd->Attributes.mTime = fullFd->Attributes.aTime = fullFd->Attributes.cTime = curTimeSpec;
        }
        else
        {
            LOG_ERROR("Error in getProperties during open for remote mountName={} path={}, errno={}",
                mountInfo.MountName,
                relativePath,
                strerror(errno));
            return -1;
        }
    }
    else
    {
        // If O_EXCL is specified with O_CREAT, and the file already exists, open() will fail.
        if (isWriteMode && (flags & O_CREAT) != 0 && (flags & O_EXCL) != 0)
        {
            LOG_ERROR("Cannot overwrite an existing file mountName={} path={}",
                mountInfo.MountName,
                relativePath);
            return -1;
        }
    }

    GetStreamingClient(mountInfo).OpenFile(relativePath);

    int fd = AllotNewFd(move(fullFd));
    if (fd == -1)
    {
        LOG_ERROR("Could not allot new fd for mountName={} relative path={}", mountInfo.MountName, relativePath);
        return -1;
    }

    LOG_DEBUG("Alloted fd={} for mountName={}, relativePath={}", fd, mountInfo.MountName, relativePath);
    return fd;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::OpenInternal(const char* path, int flags, mode_t mode, bool isOpen64)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(path, relPath);
    const bool isRelative = (mountInfo != nullptr);

    if (!isRelative)
    {
        if (!isOpen64)
        {
            return StorageLibcApiWrapper::open(path, flags, mode);
        }
        return StorageLibcApiWrapper::open64(path, flags, mode);
    }

    LOG_DEBUG(
        "Intercepted the {} system call at mountName={} path={} flags={}",
        isOpen64 ? "open64" : "open",
        mountInfo->MountName,
        path,
        flags);
    return OpenValidatedPathInternal(*mountInfo, relPath, flags);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandleRead(int fd, void *buf, size_t nBytes)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::read(fd, buf, nBytes);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::read(fd, buf, nBytes);
    }

    const off64_t readOffset = fdIt->second->FileOffset;
    const off64_t fileSize = fdIt->second->Attributes.size;
    const std::string path = fdIt->second->RemotePath;
    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepting the read system call at fd={} mountName={} path={} nBytes={} readOffset={}",
        fd,
        mountInfo.MountName,
        path,
        nBytes,
        readOffset);

    ssize_t result = GetStreamingClient(mountInfo).ReadFile(path, buf, nBytes, readOffset, fileSize);
    if (result < 0)
    {
        LOG_ERROR("StreamingClient read returned error for fd={} mountName={} path={} nBytes={} readOffset={}",
            fd,
            mountInfo.MountName,
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
            fdIt->second->FileOffset = (readOffset + result);
        }
        else
        {
            LOG_WARN("File descriptor not found post read(), skipped updating the file offset. fd={} "
                "mountName={} path={}",
                fd,
                mountInfo.MountName,
                path)
        }

        openHandlesLock.unlock();
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandlePRead(int fd, void *buf, size_t nBytes, off64_t offset)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::pread(fd, buf, nBytes, offset);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);    
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::pread(fd, buf, nBytes, offset);
    }

    const std::string path = fdIt->second->RemotePath;
    off64_t fileSize = fdIt->second->Attributes.size;
    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepting the pread system call at fd={} mountName={} path={} nBytes={} offset={}",
        fd,
        mountInfo.MountName,
        path,
        nBytes,
        offset);

    ssize_t result = GetStreamingClient(mountInfo).ReadFile(path, buf, nBytes, offset, fileSize);
    if (result < 0)
    {
        LOG_ERROR("StreamingClient read returned error for fd={} mountName={} path={} nBytes={} offset={}",
            fd,
            mountInfo.MountName,
            path,
            nBytes,
            offset);
        return result;
    }

    // Note: Unlike read(), pread() doesn't need to update the file offset post read.
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::WriteInternal(
    int fd,
    const void *buf,
    size_t nBytes,
    bool isPwrite,
    off64_t offset)
{
    if(!IsFdInRange(fd))
    {
        return isPwrite ?
            StorageLibcApiWrapper::pwrite(fd, buf, nBytes, offset) : StorageLibcApiWrapper::write(fd, buf, nBytes);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if(fdIt == m_mapOpenHandles.end())
    {
        return isPwrite ?
            StorageLibcApiWrapper::pwrite(fd, buf, nBytes, offset) : StorageLibcApiWrapper::write(fd, buf, nBytes);
    }

    if(!fdIt->second->IsWriteMode)
    {
        LOG_ERROR("Attempting to call {} on a file not opened with write flags. mountName={} path={}",
            isPwrite ? "pwrite" : "write", fdIt->second->MountName, fdIt->second->RemotePath);
        return -1;
    }
    STORAGE_ASSERT(StorageInterceptConfig::GetInstance().IsWriteSupportEnabled(), 
        "Write support must be enabled to receive write calls on write-enabled file descriptors.");
    
    if (fdIt->second->IsWriteInProgress)
    {
        LOG_ERROR("Concurrent write calls are not supported on the same file descriptor. mountName={} path={}",
            fdIt->second->MountName, fdIt->second->RemotePath);
        return -1;
    }
    fdIt->second->IsWriteInProgress = true;

    const off64_t writeOffset = isPwrite ? offset : fdIt->second->FileOffset;
    const std::string path = fdIt->second->RemotePath;
    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepting the {} system call at fd={} mountName={} path={} nBytes={} writeOffset={}",
        isPwrite ? "pwrite" : "write",
        fd,
        mountInfo.MountName,
        path,
        nBytes,
        writeOffset);
    ssize_t result = GetStreamingClient(mountInfo).WriteFile(path, buf, nBytes, writeOffset);
    if(result < 0)
    {
        LOG_ERROR("StreamingClient write returned error for fd={} mountName={} path={} nBytes={} writeOffset={}",
            fd,
            mountInfo.MountName,
            path,
            nBytes,
            writeOffset);
        return result;
    }

    openHandlesLock.lock();
    fdIt = m_mapOpenHandles.find(fd);
    if(fdIt != m_mapOpenHandles.end())
    {
        if (!isPwrite) fdIt->second->FileOffset = (writeOffset + result);
        fdIt->second->IsWriteInProgress = false;
    }
    else
    {
        LOG_WARN("File descriptor not found post write(), skipped updating the file offset. fd={} "
            "mountName={} path={}",
            fd,
            mountInfo.MountName,
            path)
    }
    openHandlesLock.unlock();
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandleWrite(
    /*[in] */ int fd,
    /*[in] */ const void *buf,
    /*[in] */ size_t nBytes)
{
    return WriteInternal(fd, buf, nBytes, false /* isPwrite */, 0 /* offset */);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t ContentHandler::HandlePWrite(
    /*[in] */ int fd,
    /*[in] */ const void *buf,
    /*[in] */ size_t nBytes,
    /*[in] */ off64_t offset)
{
    return WriteInternal(fd, buf, nBytes, true /* isPwrite */, offset);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t ContentHandler::HandleLseek(int fd, off64_t offset, int whence)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::lseek64(fd, offset, whence);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::lseek64(fd, offset, whence);
    }

    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    off64_t nextOffset = fdIt->second->FileOffset;
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
            std::string path = fdIt->second->RemotePath;
            off64_t fileSize = fdIt->second->Attributes.size;

            /* offset > 0 is currently not supported for SEEK_END case */
            if (offset > 0)
            {
                LOG_ERROR(
                    "Lseek request to an offset beyond file size encountered. path={}, mountName={},"
                    " requestedOffset={}, fileSize={}",
                    path,
                    mountInfo.MountName,
                    fileSize + offset,
                    fileSize);
                nextOffset = -1;
                return nextOffset;
            }

            isModified = (fileSize + offset) ^ nextOffset;
            nextOffset = fileSize + offset;
            break;

    }
    if (isModified == 0)
    {
        return nextOffset;
    }

    fdIt->second->FileOffset = nextOffset;
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepted the lseek system call and updated offset. fd={} mountName={}"
        " offset={} whence={} nextOffset={}",
        fd,
        mountInfo.MountName,
        offset,
        whence,
        nextOffset);
    return nextOffset;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleClose(int fd)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::close(fd);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        return StorageLibcApiWrapper::close(fd);
    }

    const std::string remotePath = fdIt->second->RemotePath;
    const bool isWriteMode = fdIt->second->IsWriteMode;
    MountInfo& mountInfo = GetMountInfo(fdIt->second->MountName);
    openHandlesLock.unlock();

    // TODO: StreamingClient throws on close errors, change to return codes?
    GetStreamingClient(mountInfo).Close(remotePath, isWriteMode);

    openHandlesLock.lock();
    fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        LOG_WARN("File descriptor not found post close(), skipped updating the open files metadata. fd={} "
            "mountName={} path={}",
            fd,
            mountInfo.MountName,
            remotePath)
    }
    else
    {
        m_mapOpenHandles.erase(fdIt);
    }
    auto fdListIt = m_mapFileToFds.find(remotePath);
    if (fdListIt != m_mapFileToFds.end())
    {
        fdListIt->second.erase(fd);
        if (fdListIt->second.empty())
        {
            // No more open file descriptors for this file, remove the metadata entry
            m_mapFileToFds.erase(fdListIt);
        }
    }
    else
    {
        LOG_WARN("File path not found in file to fds map post close(), skipped updating it."
            " fd={} mountName={} path={}",
            fd,
            mountInfo.MountName,
            remotePath)
    }
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepted the close system call at fd={} mountName={} path={}",
        fd,
        mountInfo.MountName,
        remotePath);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void* ContentHandler::HandleOpendir(const char* path)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(path, relPath);
    const bool isRelative = (mountInfo != nullptr);

    if (!isRelative)
    {
        return StorageLibcApiWrapper::opendir(path);
    }

    LOG_DEBUG("Intercepting the opendir system call for path={} mountName={}", path, mountInfo->MountName);

    std::unique_ptr<DirectoryStream> dirStream = std::make_unique<DirectoryStream>();
    dirStream->MountName = mountInfo->MountName;
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
void* ContentHandler::HandleReaddir(void* dirp, bool isRead64)
{
    std::unique_lock<std::mutex> dirStreamLock(m_lockDirectoryStreams);
    if (m_mapDirectoryStreams.find(static_cast<DirectoryStream *>(dirp)) == m_mapDirectoryStreams.end())
    {
        dirStreamLock.unlock();
        if (isRead64)
        {
            return StorageLibcApiWrapper::readdir64(dirp);
        }
        return StorageLibcApiWrapper::readdir(dirp);
    }
    dirStreamLock.unlock();

    DirectoryStream& dirStream = *(static_cast<DirectoryStream *>(dirp));
    MountInfo& mountInfo = GetMountInfo(dirStream.MountName);
    LOG_DEBUG("Intercepting the {} system call for path={} mountName={}",
        isRead64 ? "readdir64" : "readdir",
        dirStream.RemotePath,
        mountInfo.MountName);

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
        newContinuationToken = GetStreamingClient(mountInfo).ReadDirectory(
            dirStream.RemotePath,
            dirStream.ContinuationToken,
            dirStream.Entries);

        dirStream.ContinuationToken = newContinuationToken;
        if (newContinuationToken == "")
        {
            dirStream.AllEntriesFetched = true;
        }

        // Retry reading with newly fetched directory entries.
        return HandleReaddir(dirp, isRead64);
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
    auto dirIter = m_mapDirectoryStreams.find(static_cast<DirectoryStream *>(dirp));
    if (dirIter == m_mapDirectoryStreams.end())
    {
        dirStreamLock.unlock();
        return StorageLibcApiWrapper::closedir(dirp);
    }

    LOG_DEBUG("Intercepting the closedir system call for path={} mountName={}",
        (((DirectoryStream *)dirp)->RemotePath),
        (((DirectoryStream *)dirp)->MountName));
    m_mapDirectoryStreams.erase(dirIter);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleAccess(const char* pathname, int mode)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::access(pathname, mode);
    }

    LOG_DEBUG("Intercepted access call for path={} mountName={}", pathname, mountInfo->MountName);
    if ((mode & W_OK) == W_OK)
    {
        // We don't allow write access - revisit if FUSE support for this API is added
        LOG_ERROR("Write access not permitted for file={} mountName={}", pathname, mountInfo->MountName);
        errno = EACCES;
        return -1;
    }

    // Check if file exists and return status
    struct stat64 stat64buf;
    return GetPropertiesInternal(*mountInfo, relPath, "access", &stat64buf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleStat(const char* pathname, struct stat* statbuf)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::stat(pathname, statbuf);
    }

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(*mountInfo, relPath, "stat", &stat64buf);
    if (result == 0)
    {
        CopyStat64ToStat(&stat64buf, statbuf);
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleStat64(const char* pathname, struct stat64* statbuf)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::stat64(pathname, statbuf);
    }

    return GetPropertiesInternal(*mountInfo, relPath, "stat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleXstat(int __ver, const char* pathname, struct stat* statbuf)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::xstat(__ver, pathname, statbuf);
    }

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(*mountInfo, relPath, "xstat", &stat64buf);
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
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::xstat64(__ver, pathname, statbuf);
    }

    return GetPropertiesInternal(*mountInfo, relPath, "xstat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFstat(int fd, struct stat* statbuf)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::fstat(fd, statbuf);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::fstat(fd, statbuf);
    }
    FileAttributes attributes = fdIt->second->Attributes;
    openHandlesLock.unlock();

    struct stat64 stat64buf;
    PopulateStatBuf(attributes, &stat64buf);
    CopyStat64ToStat(&stat64buf, statbuf);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFstat64(int fd, struct stat64* statbuf)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::fstat64(fd, statbuf);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::fstat64(fd, statbuf);
    }

    FileAttributes attributes = fdIt->second->Attributes;
    openHandlesLock.unlock();

    LOG_DEBUG("Intercepted fstat64 call for fd={} path={} mountName={}",
        fd,
        fdIt->second->RemotePath,
        fdIt->second->MountName);
    PopulateStatBuf(attributes, statbuf);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFxstat(int __ver, int fd, struct stat* statbuf)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::fxstat(__ver, fd, statbuf);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::fxstat(__ver, fd, statbuf);
    }
    FileAttributes attributes = fdIt->second->Attributes;
    openHandlesLock.unlock();

    struct stat64 stat64buf;
    PopulateStatBuf(attributes, &stat64buf);
    CopyStat64ToStat(&stat64buf, statbuf);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFxstat64(int __ver, int fd, struct stat64* statbuf)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::fxstat64(__ver, fd, statbuf);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::fxstat64(__ver, fd, statbuf);
    }
    FileAttributes attributes = fdIt->second->Attributes;
    openHandlesLock.unlock();

    PopulateStatBuf(attributes, statbuf);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleLxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    std::string relPath;
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::lxstat(__ver, pathname, statbuf);
    }

    struct stat64 stat64buf;
    int result = GetPropertiesInternal(*mountInfo, relPath, "lxstat", &stat64buf);
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
    MountInfo* mountInfo = CheckInterceptionForPath(pathname, relPath);
    const bool isRelative = (mountInfo != nullptr);
    if (!isRelative)
    {
        return StorageLibcApiWrapper::lxstat64(__ver, pathname, statbuf);
    }

    return GetPropertiesInternal(*mountInfo, relPath, "lxstat64", statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::GetPropertiesInternal(
    MountInfo& mountInfo,
    const std::string& relPath,
    const char* interceptedCall,
    struct stat64* statbuf)
{
    LOG_DEBUG("Intercepting {} call for remote path={} mountName={}", interceptedCall, relPath, mountInfo.MountName);

    FileAttributes attributes;
    int result = GetStreamingClient(mountInfo).GetProperties(relPath, attributes);
    if (result < 0)
    {
        LOG_ERROR("Error in {} call for remote path={}, mountName={}, errno={}",
            interceptedCall,
            relPath,
            mountInfo.MountName,
            strerror(errno));
        result = -1;
        return result;
    }

    PopulateStatBuf(attributes, statbuf);
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleIoctl(int fd, unsigned long request, void* argp)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::ioctl(fd, request, argp);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::ioctl(fd, request, argp);
    }

    LOG_INFO("Intercepting the ioctl call for fd={} request={} path={} mountName={}",
        fd,
        request,
        fdIt->second->RemotePath,
        fdIt->second->MountName);

    // Note: ioctl() is called by python's pathlib module during file open with request as FIOCLEX, which is a no-op
    //       for us.
    // TODO: see if there are any other use-cases which call ioctl() and handle accordingly.
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleIsatty(int fd)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::isatty(fd);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::isatty(fd);
    }

    LOG_DEBUG("Intercepting the isatty call for fd={} path={} mountName={}",
        fd,
        fdIt->second->RemotePath,
        fdIt->second->MountName);

    // isatty() is supposed to return 1 if fd refers to a terminal and 0 otherwise (which is the case here).
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::HandleFlock(int fd, int operation)
{
    if (!IsFdInRange(fd))
    {
        return StorageLibcApiWrapper::flock(fd, operation);
    }

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        openHandlesLock.unlock();
        return StorageLibcApiWrapper::flock(fd, operation);
    }

    LOG_DEBUG(
        "Intercepting the flock call for fd={} path={} mountName={} operation={} LOCK_SH={} LOCK_EX={} LOCK_NB={}",
        fd,
        fdIt->second->RemotePath,
        fdIt->second->MountName,
        operation,
        (operation & LOCK_SH),
        (operation & LOCK_EX),
        (operation & LOCK_NB));

    // Note: Only use case we have encountered for flock() is taking shared locks on a file before reading it.
    //       We would need to implement locking here if we encounter use cases where an exclusive lock is taken.
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::Handleclearerr(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::clearerr(stream);
    }

    LOG_ERROR("clearerr interception not yet supported, fd={}", fd);
    throw "clearerr interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefeof(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::feof(stream);
    }

    LOG_ERROR("feof interception not yet supported, fd={}", fd);
    throw "feof interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handleferror(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::ferror(stream);
    }

    LOG_ERROR("ferror interception not yet supported, fd={}", fd);
    throw "ferror interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefflush(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fflush(stream);
    }

    LOG_ERROR("fflush interception not yet supported, fd={}", fd);
    throw "fflush interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefgetc(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fgetc(stream);
    }

    LOG_ERROR("fgetc interception not yet supported, fd={}", fd);
    throw "fgetc interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefgetpos(void* stream, void* pos, bool is64Bit)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        if (is64Bit)
        {
            return StorageLibcApiWrapper::fgetpos64(stream, pos);
        }
        return StorageLibcApiWrapper::fgetpos(stream, pos);
    }

    LOG_ERROR("fgetpos{} interception not yet supported, fd={}", is64Bit ? "64" : "", fd);
    throw "fgetpos[64] interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
char* ContentHandler::Handlefgets(char* s, int size, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fgets(s, size, stream);
    }

    LOG_ERROR("fgets interception not yet supported, fd={}", fd);
    throw "fgets interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefpurge(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fpurge(stream);
    }

    LOG_ERROR("fpurge interception not yet supported, fd={}", fd);
    throw "fpurge interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefputc(int c, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fputc(c, stream);
    }

    LOG_ERROR("fputc interception not yet supported, fd={}", fd);
    throw "fputc interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefputs(const char* s, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fputs(s, stream);
    }

    LOG_ERROR("fputs interception not yet supported, fd={}", fd);
    throw "fputs interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
size_t ContentHandler::Handlefread(void* ptr, size_t size, size_t nmemb, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fread(ptr, size, nmemb, stream);
    }

    LOG_ERROR("fread interception not yet supported, fd={}", fd);
    throw "fread interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
void* ContentHandler::Handlefreopen(const char* pathname, const char* mode, void* stream, bool is64Bit)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        if (is64Bit)
        {
            return StorageLibcApiWrapper::freopen64(pathname, mode, stream);
        }
        return StorageLibcApiWrapper::freopen(pathname, mode, stream);
    }

    LOG_ERROR("freopen{} interception not yet supported, fd={}", is64Bit ? "64" : "", fd);
    throw "freopen[64] interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefseek(void* stream, long offset, int whence)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fseek(stream, offset, whence);
    }

    LOG_ERROR("fseek interception not yet supported, fd={}", fd);
    throw "fseek interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlefsetpos(void* stream, const void* pos, bool is64Bit)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        if (is64Bit)
        {
            return StorageLibcApiWrapper::fsetpos64(stream, pos);
        }
        return StorageLibcApiWrapper::fsetpos(stream, pos);
    }

    LOG_ERROR("fsetpos{} interception not yet supported, fd={}", is64Bit ? "64" : "", fd);
    throw "fsetpos[64] interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
long ContentHandler::Handleftell(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::ftell(stream);
    }

    LOG_ERROR("ftell interception not yet supported, fd={}", fd);
    throw "ftell interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
size_t ContentHandler::Handlefwrite(const void* ptr, size_t size, size_t nmemb, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::fwrite(ptr, size, nmemb, stream);
    }

    LOG_ERROR("fwrite interception not yet supported, fd={}", fd);
    throw "fwrite interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlegetc(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::_getc(stream);
    }

    LOG_ERROR("getc interception not yet supported, fd={}", fd);
    throw "getc interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlegetw(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::getw(stream);
    }

    LOG_ERROR("getw interception not yet supported, fd={}", fd);
    throw "getw interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handleputc(int c, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::_putc(c, stream);
    }

    LOG_ERROR("putc interception not yet supported, fd={}", fd);
    throw "putc interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handleputw(int w, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::putw(w, stream);
    }

    LOG_ERROR("putw interception not yet supported, fd={}", fd);
    throw "putw interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::Handlerewind(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::rewind(stream);
    }

    LOG_ERROR("rewind interception not yet supported, fd={}", fd);
    throw "rewind interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::Handlesetbuf(void* stream, char* buf)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::setbuf(stream, buf);
    }

    LOG_ERROR("setbuf interception not yet supported, fd={}", fd);
    throw "setbuf interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::Handlesetbuffer(void* stream, char* buf, size_t size)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::setbuffer(stream, buf, size);
    }

    LOG_ERROR("setbuffer interception not yet supported, fd={}", fd);
    throw "setbuffer interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
void ContentHandler::Handlesetlinebuf(void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::setlinebuf(stream);
    }

    LOG_ERROR("setlinebuf interception not yet supported, fd={}", fd);
    throw "setlinebuf interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlesetvbuf(void* stream, char* buf, int mode, size_t size)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::setvbuf(stream, buf, mode, size);
    }

    LOG_ERROR("setvbuf interception not yet supported, fd={}", fd);
    throw "setvbuf interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handleungetc(int c, void* stream)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::ungetc(c, stream);
    }

    LOG_ERROR("ungetc interception not yet supported, fd={}", fd);
    throw "ungetc interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlevfprintf(void* stream, const char* format, va_list ap)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::vfprintf(stream, format, ap);
    }

    LOG_ERROR("vfprintf interception not yet supported, fd={}", fd);
    throw "vfprintf interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::Handlevfscanf(void* stream, const char* format, va_list ap)
{
    int fd = GetFdFromStream(stream);
    if (fd < 0)
    {
        return StorageLibcApiWrapper::vfscanf(stream, format, ap);
    }

    LOG_ERROR("vfscanf interception not yet supported, fd={}", fd);
    throw "vfscanf interception not yet supported";
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::GetFdFromStream(void* stream)
{
    // If stream is valid and _fileno is set to -1, we know that this is managed by us and extract the fd from _flags.
    FILE* fileStreamPtr = static_cast<FILE*>(stream);
    if (fileStreamPtr == nullptr || fileStreamPtr->_fileno != -1 || !IsFdInRange(fileStreamPtr->_flags))
    {
        return -1;
    }

    int fd = fileStreamPtr->_flags;
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdIt = m_mapOpenHandles.find(fd);
    if (fdIt == m_mapOpenHandles.end())
    {
        return -1;
    }

    return fd;
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t ContentHandler::GetOpenFileHandleCount(const std::string& remotePath)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdListIt = m_mapFileToFds.find(remotePath);
    if (fdListIt == m_mapFileToFds.end())
    {
        return 0;
    }

    return fdListIt->second.size();
}
//---------------------------------------------------------------------------------------------------------------------
bool ContentHandler::IsOpenedForWrite(const std::string& remotePath)
{
    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    auto fdListIt = m_mapFileToFds.find(remotePath);
    if (fdListIt == m_mapFileToFds.end())
    {
        return false;
    }

    // Loop over open FDs and check if any is open for write
    for (const int fd : fdListIt->second)
    {
        auto fdIt = m_mapOpenHandles.find(fd);
        if (fdIt != m_mapOpenHandles.end() && fdIt->second->IsWriteMode)
        {
            return true;
        }
    }

    return false;
}
//---------------------------------------------------------------------------------------------------------------------
int ContentHandler::AllotNewFd(std::unique_ptr<FileDescriptor> fd)
{
    const uint8_t maxAttempts = 5;
    uint8_t nAttempts = 0;

    const bool isWriteMode = fd->IsWriteMode;
    const std::string remotePath = fd->RemotePath;

    std::unique_lock<std::mutex> openHandlesLock(m_lockOpenHandles);
    while(nAttempts < maxAttempts)
    {
        // FD_VALUE_START is added to avoid collision with system alloted fds.
        int newFd = rand() % FD_VALUE_RANGE + FD_VALUE_START;
        if (m_mapOpenHandles.find(newFd) == m_mapOpenHandles.end())
        {
            // If file stream is enabled, set the fd values appropriately
            // For a valid (non-intercepted) file the fileno will be >= 0. Hence setting this to -1 to distinguish
            // it from external streams. We are now free to use other fields (_flags) for setting our fd value.
            if (fd->FileStream)
            {
                fd->FileStream->_fileno = -1;
                fd->FileStream->_flags = newFd;
            }
            m_mapOpenHandles[newFd] = std::move(fd);
            m_mapFileToFds[remotePath].insert(newFd);
            return newFd;
        }

        nAttempts++;
    }

    LOG_ERROR("Could not get new fd after {} attempts", nAttempts);
    return -1;
}
//---------------------------------------------------------------------------------------------------------------------
bool ContentHandler::IsFdInRange(uint64_t fd)
{
    // Check if fd is within our alloted range
    if (fd < FD_VALUE_START || fd >= (FD_VALUE_START + FD_VALUE_RANGE))
    {
        return false;
    }

    return true;
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
    MountInfo* mountInfo = CheckInterceptionForPath(path, relPath);
    const bool isRelative = (mountInfo != nullptr);

    if (isRelative)
    {
        /* Listxattr and other extended attributes related calls' interception is currently not implemented */
        LOG_INFO(
            "Listxattr called at mountName: {}, path: {} is not intercepted, falling back to it's original implementation",
            mountInfo->MountName,
            path);
    }
    return StorageLibcApiWrapper::listxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------
