///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <map>
#include <mutex>
#include "DirectoryStream.h"
#include "FileAttributes.h"
#include "FileDescriptor.h"
#include "StreamingClient.h"
//---------------------------------------------------------------------------------------------------------------------
class ContentHandler
{
    public:

        /* Singleton */
        static ContentHandler& GetInstance();

        int HandleOpen(
            /*[in]*/ const char* path,
            /*[in]*/ int flags);

        int HandleOpen(
            /*[in]*/ const char* path,
            /*[in]*/ int flags,
            /*[in]*/ mode_t mode);

        int HandleOpen64(
            /*[in]*/ const char* path,
            /*[in]*/ int flags);

        int HandleOpen64(
            /*[in]*/ const char* path,
            /*[in]*/ int flags,
            /*[in]*/ mode_t mode);

        void* HandleFopen(
            /*[in]*/ const char* path,
            /*[in]*/ const char* mode,
            /*[in]*/ bool isOpen64);

        int HandleFileNo(
            /*[in]*/ void* stream);

        int HandleFClose(
            /*[in]*/ void* stream);

        int HandleOpenAt(
            /*[in]*/ int dirfd,
            /*[in]*/ const char* path,
            /*[in]*/ int flags,
            /*[in]*/ mode_t mode,
            /*[in]*/ bool isOpen64
            );

        ssize_t HandleRead(
            /*[in] */ int fd,
            /*[out]*/ void *buf,
            /*[in] */ size_t nByte);

        ssize_t HandlePRead(
            /*[in] */ int fd,
            /*[out]*/ void *buf,
            /*[in] */ size_t nByte,
            /*[in] */ off64_t offset);

        ssize_t HandleWrite(
            /*[in] */ int fd,
            /*[in] */ const void *buf,
            /*[in] */ size_t nByte);
        
        ssize_t HandlePWrite(
            /*[in] */ int fd,
            /*[in] */ const void *buf,
            /*[in] */ size_t nByte,
            /*[in] */ off64_t offset);

        off64_t HandleLseek(
            /*[in]*/ int fd,
            /*[in]*/ off64_t offset,
            /*[in]*/ int whence);

        int HandleClose(/*[in]*/ int fd);

        void* HandleOpendir(/*[in]*/ const char* path);

        void* HandleReaddir(/*[in]*/ void* dirp, bool isRead64);

        int HandleClosedir(/*[in]*/ void* dirp);

        int HandleAccess(
            /*[in] */ const char* pathname,
            /*[in]*/ int mode);
        
        int HandleStat(
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat* statbuf);
        
        int HandleStat64(
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat64* statbuf);

        int HandleXstat(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat* statbuf);

        int HandleXstat64(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat64* statbuf);

        int HandleFstat(
            /*[in] */ int fd,
            /*[out]*/ struct stat* statbuf);

        int HandleFstat64(
            /*[in] */ int fd,
            /*[out]*/ struct stat64* statbuf);

        int HandleFxstat(
            /*[in] */ int __ver,
            /*[in] */ int fd,
            /*[out]*/ struct stat* statbuf);

        int HandleFxstat64(
            /*[in] */ int __ver,
            /*[in] */ int fd,
            /*[out]*/ struct stat64* statbuf);
        
        int HandleLxstat(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat* statbuf);
        
        int HandleLxstat64(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat64* statbuf);

        int HandleIoctl(
            /*[in] */ int fd,
            /*[in] */ unsigned long request,
            /*[out] */ void* argp);

        int HandleIsatty(/*[in]*/ int fd);

        int HandleFlock(/*[in]*/ int fd, /*[in]*/ int operation);

        ssize_t HandleListxattr(
            /*[in]*/ const char* path,
            /*[out]*/ char* list,
            /*[in]*/ size_t size);

        void Handleclearerr(
            /*[in]*/ void* stream);

        int Handlefeof(
            /*[in]*/ void* stream);

        int Handleferror(
            /*[in]*/ void* stream);

        int Handlefflush(
            /*[in]*/ void* stream);

        int Handlefgetc(
            /*[in]*/ void* stream);

        int Handlefgetpos(
            /*[in]*/ void* stream,
            /*[out]*/ void* pos,
            /*[in]*/ bool is64Bit);

        char* Handlefgets(
            /*[out]*/ char* s,
            /*[in]*/ int size,
            /*[in]*/ void* stream);

        int Handlefpurge(
            /*[in]*/ void* stream);

        int Handlefputc(
            /*[in]*/ int c,
            /*[in]*/ void* stream);

        int Handlefputs(
            /*[in]*/ const char* s,
            /*[in]*/ void* stream);

        size_t Handlefread(
            /*[out]*/ void* ptr,
            /*[in]*/ size_t size,
            /*[in]*/ size_t nmemb,
            /*[in]*/ void* stream);

        void* Handlefreopen(
            /*[in]*/ const char* pathname,
            /*[in]*/ const char* mode,
            /*[in]*/ void* stream,
            /*[in]*/ bool is64Bit);

        int Handlefseek(
            /*[in]*/ void* stream,
            /*[in]*/ long offset,
            /*[in]*/ int whence);

        int Handlefsetpos(
            /*[in]*/ void* stream,
            /*[in]*/ const void* pos,
            /*[in]*/ bool is64Bit);

        long Handleftell(
            /*[in]*/ void* stream);

        size_t Handlefwrite(
            /*[in]*/ const void* ptr,
            /*[in]*/ size_t size,
            /*[in]*/ size_t nmemb,
            /*[in]*/ void* stream);

        int Handlegetc(
            /*[in]*/ void* stream);

        int Handlegetw(
            /*[in]*/ void* stream);

        int Handleputc(
            /*[in]*/ int c,
            /*[in]*/ void* stream);

        int Handleputw(
            /*[in]*/ int w,
            /*[in]*/ void* stream);

        void Handlerewind(
            /*[in]*/ void* stream);

        void Handlesetbuf(
            /*[in]*/ void* stream,
            /*[in]*/ char* buf);

        void Handlesetbuffer(
            /*[in]*/ void* stream,
            /*[in]*/ char* buf,
            /*[in]*/ size_t size);

        void Handlesetlinebuf(
            /*[in]*/ void* stream);

        int Handlesetvbuf(
            /*[in]*/ void* stream,
            /*[in]*/ char* buf,
            /*[in]*/ int mode,
            /*[in]*/ size_t size);

        int Handleungetc(
            /*[in]*/ int c,
            /*[in]*/ void* stream);

        int Handlevfprintf(
            /*[in]*/ void* stream,
            /*[in]*/ const char* format,
            /*[in]*/ va_list ap);

        int Handlevfscanf(
            /*[in]*/ void* stream,
            /*[in]*/ const char* format,
            /*[out]*/ va_list ap); 

        // APIs to handle shared state when a process is forked.
        // Processes which are forked share the system state including memory and open file descriptors.
        // We need to ensure network connections (sockets) are not shared across multiple processes.
        static void ForkHandlerPrepare();
        static void ForkHandlerParent();
        static void ForkHandlerChild();

    private:
        ContentHandler();
    
    private:
        // Per-mount information
        struct MountInfo
        {
            MountInfo(const std::string& name, const std::string& path)
                : MountName(name)
                , MountPath(path)
            {}

            const std::string MountName;
            const std::string MountPath;

            std::unique_ptr<StreamingClient> StreamingClientPtr;
            std::mutex                       LockStreamingClient;
        };

    private:
        void LoadMountInfoConfig();

        MountInfo* CheckInterceptionForPath(
            /*[in] */ const char* path,
            /*[out]*/ std::string& outRelativePath);
        
        MountInfo& GetMountInfo(/*[in] */ const std::string& mountName);

        StreamingClient& GetStreamingClient(/*[in]*/ MountInfo& mountInfo);

        void ClearCacheClients();
        void SetCacheClients();

        int OpenValidatedPathInternal(
            /*[in]*/ MountInfo& mountInfo,
            /*[in]*/ const std::string& relativePath,
            /*[in]*/ int flags,
            /*[in]*/ std::unique_ptr<FILE> fileStream = nullptr);

        int OpenInternal(
            /*[in]*/ const char* path,
            /*[in]*/ int flags,
            /*[in]*/ mode_t mode,
            /*[in]*/ bool isOpen64);
        
        ssize_t WriteInternal(
            /*[in] */ int fd,
            /*[in] */ const void *buf,
            /*[in] */ size_t nByte,
            /*[in] */ bool isPwrite,
            /*[in] */ off64_t offset);

        // Helper method to add a unique fd for a new open.
        // Returns new fd in case of success, -1 in case of error
        int AllotNewFd(/*[in]*/ std::unique_ptr<FileDescriptor> fd);

        // Check if fd is within our internal range
        bool IsFdInRange(/*[in]*/ uint64_t fd);

        int GetPropertiesInternal(
            /*[in]*/ MountInfo& mountInfo,
            /*[in]*/ const std::string& relPath,
            /*[in]*/ const char* interceptedCall,
            /*[out]*/ struct stat64* statbuf);

        void PopulateStatBuf(
            /*[in] */ const FileAttributes& stats,
            /*[out]*/ struct stat64* statbuf);
        
        void CopyStat64ToStat(
            /*[in] */ const struct stat64* stat64buf,
            /*[out]*/ struct stat* statbuf);

        // Get FD value if the stream was managed by us, if not return -1
        int GetFdFromStream(/*[in]*/ void* stream);

        // Get the count of open file handles for a given remote path
        uint32_t GetOpenFileHandleCount(
            /*[in]*/ const std::string& remotePath);
        
        // Check if a given remote path is opened for write
        bool IsOpenedForWrite(
            /*[in]*/ const std::string& remotePath);

    private:
        // file fd allocated range (min/max) we use internally
        static constexpr uint32_t                                    FD_VALUE_START = 90000;
        static constexpr uint32_t                                    FD_VALUE_RANGE = 100000;

        // Need to store the error string in a thread local variable
        // so that the calling application will always have access to it
        static thread_local std::string                              m_threadLocalLastError;

        bool                                                         m_bForkHandlersSetup;

        std::vector<std::unique_ptr<MountInfo>>                      m_vecMountInfo;

        // Cache clients
        std::shared_ptr<ICacheClient>                                m_spCacheClient;
        std::shared_ptr<IAttributesCache>                            m_spAttributesCache;
        bool                                                         m_bCacheClientsInitialized;
        std::mutex                                                   m_lockCacheClients;

        // Open file handles
        // Map of open handles by fd. Key is the fd value, value is the FileDescriptor structure
        std::map<int, std::unique_ptr<FileDescriptor>>               m_mapOpenHandles;
        // Map of fds opened for each file. Key is file relative path, value is set of open fds
        std::map<std::string, std::set<int>>                         m_mapFileToFds;
        std::mutex                                                   m_lockOpenHandles;

        std::map<DirectoryStream*, std::unique_ptr<DirectoryStream>> m_mapDirectoryStreams;
        std::mutex                                                   m_lockDirectoryStreams;
};
//---------------------------------------------------------------------------------------------------------------------