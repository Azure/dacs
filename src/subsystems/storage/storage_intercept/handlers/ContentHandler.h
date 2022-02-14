///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include "DirectoryStream.h"
#include "FileAttributes.h"
#include "FileDescriptor.h"
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

        ssize_t HandleRead(
            /*[in] */ int fd,
            /*[out]*/ void *buf,
            /*[in] */ size_t nByte);

        ssize_t HandlePRead(
            /*[in] */ int fd,
            /*[out]*/ void *buf,
            /*[in] */ size_t nByte,
            /*[in] */ off64_t offset);

        off64_t HandleLseek(
            /*[in]*/ int fd,
            /*[in]*/ off64_t offset,
            /*[in]*/ int whence);

        int HandleClose(/*[in]*/ int fd);

        void* HandleOpendir(/*[in]*/ const char* path);

        void* HandleReaddir64(/*[in]*/ void* dirp);

        int HandleClosedir(/*[in]*/ void* dirp);

        int HandleXstat(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
            /*[out]*/ struct stat* statbuf);

        int HandleXstat64(
            /*[in] */ int __ver,
            /*[in] */ const char* pathname,
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

    private:
        ContentHandler();

    private:
        int OpenInternal(
            /*[in]*/ const char* path,
            /*[in]*/ int flags,
            /*[in]*/ mode_t mode,
            /*[in]*/ bool isOpen64);

        // Helper method to add a unique fd for a new open.
        // Returns new fd in case of success, -1 in case of error
        int AllotNewFd(/*[in]*/ const FileDescriptor& fd);

        int GetPropertiesInternal(
            /*[in]*/ const std::string& relPath,
            /*[in]*/ const char* interceptedCall,
            /*[out]*/ struct stat64* statbuf);

        void PopulateStatBuf(
            /*[in] */ const FileAttributes& stats,
            /*[out]*/ struct stat64* statbuf);
        
        void CopyStat64ToStat(
            /*[in] */ const struct stat64* stat64buf,
            /*[out]*/ struct stat* statbuf);

    private:
        const std::string                                            m_strStorageRoot;

        std::map<int, FileDescriptor>                                m_mapOpenHandles;
        std::mutex                                                   m_lockOpenHandles;

        std::map<DirectoryStream*, std::unique_ptr<DirectoryStream>> m_mapDirectoryStreams;
        std::mutex                                                   m_lockDirectoryStreams;
};
//---------------------------------------------------------------------------------------------------------------------