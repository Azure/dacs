///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// The StorageDlsymWrapper provides the original libc implementation of the intercepted libc calls, in case the intercepted
// libc calls need to fallback to their original counterpart. It loads the "next" libc call in the load order.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
// TODO: simplify this class using macros
//---------------------------------------------------------------------------------------------------------------------
class StorageDlsymWrapper
{
    public:
        static int open(/*[in]*/ const char* pathname,
                        /*[in]*/ int flags,
                        /*[in]*/ mode_t mode);

        static int open64(/*[in]*/ const char* pathname,
                          /*[in]*/ int flags,
                          /*[in]*/ mode_t mode);

        static off64_t lseek64(/*[in]*/ int fd,
                             /*[in]*/ off64_t offset,
                             /*[in]*/ int whence);

        static int close(/*[in]*/ int fd);

        static int xstat(/*[in]*/ int __ver,
                         /*[in]*/ const char* pathname,
                         /*[in]*/ struct stat* statbuf);

        static int xstat64(/*[in]*/ int __ver,
                         /*[in]*/ const char* pathname,
                         /*[in]*/ struct stat64* statbuf);

        static int lxstat(/*[in]*/ int __ver,
                          /*[in]*/ const char* pathname,
                          /*[in]*/ struct stat* statbuf);

        static int lxstat64(/*[in]*/ int __ver,
                          /*[in]*/ const char* pathname,
                          /*[in]*/ struct stat64* statbuf);

        static int fxstat(/*[in]*/ int __ver,
                          /*[in]*/ int fd,
                          /*[in]*/ struct stat* statbuf);
        
        static int fxstat64(/*[in]*/ int __ver,
                            /*[in]*/ int fd,
                            /*[in]*/ struct stat64* statbuf);

        static ssize_t read(/*[in]*/ int fd,
                            /*[out]*/ void *buf,
                            /*[in]*/ size_t nByte);

        static ssize_t pread(/*[in]*/ int fd,
                             /*[out]*/ void *buf,
                             /*[in]*/ size_t nByte,
                             /*[in]*/ off64_t offset);

        static void* opendir(/*[in]*/ const char* pathname);

        static void* readdir64(/*[in]*/ void* dirp);

        static int closedir(/*[in]*/ void* dirp);

        static int ioctl(/*[in]*/ int fd, /*[in]*/ unsigned long request, /*[out]*/ void* argp);
        static int isatty(/*[in]*/ int fd);
        static int flock(/*[in]*/ int fd, /*[in]*/ int operation);
        static ssize_t listxattr(/*[in]*/ const char* path, /*[out]*/ char* list, /*[in]*/ size_t size);

        typedef int (*Original_open) (/*[in]*/ const char* pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode);
        typedef int (*Original_open64) (/*[in]*/ const char* pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode);
        typedef int (*Original_close) (/*[in]*/ int fd);
        typedef ssize_t (*Original_read) (/*[in]*/ int fd, /*[out]*/ void *buf, /*[in]*/ size_t nByte);
        typedef ssize_t (*Original_pread) (/*[in]*/ int fd, /*[out]*/ void *buf, /*[in]*/ size_t nByte, /*[in]*/ off64_t offset);
        typedef int (*Original_openat) (/*[in]*/ int dirfd, /*[in]*/ const char* pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode);
        typedef off64_t (*Original_lseek64) (/*[in]*/ int fd, /*[in]*/ off64_t offset, /*[in]*/ int whence);
        typedef void* (*Original_opendir) (/*[in]*/ const char* pathname);
        typedef void* (*Original_readdir64)(/*[in]*/ void* dirp);
        typedef int (*Original_closedir)(/*[in]*/ void* dirp);
        typedef int (*Original_xstat)(/*[in]*/ int __ver, /*[in]*/ const char* pathname, /*[out]*/ struct stat* statbuf);
        typedef int (*Original_xstat64)(/*[in]*/ int __ver, /*[in]*/ const char* pathname, /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_fxstat)(/*[in]*/ int __ver, /*[in]*/ int fd, /*[out]*/ struct stat* statbuf);
        typedef int (*Original_fxstat64)(/*[in]*/ int __ver, /*[in]*/ int fd, /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_lxstat)(/*[in]*/ int __ver, /*[in]*/ const char* pathname, /*[out]*/ struct stat* statbuf);
        typedef int (*Original_lxstat64)(/*[in]*/ int __ver, /*[in]*/ const char* pathname, /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_ioctl)(/*[in]*/ int fd, /*[in]*/ unsigned long request, /*[out]*/ void* argp);
        typedef int (*Original_isatty)(/*[in]*/ int fd);
        typedef int (*Original_flock)(/*[in]*/ int fd, /*[in]*/ int operation);
        typedef ssize_t (*Original_listxattr)(/*[in]*/ const char* path, /*[out]*/ char* list, /*[in]*/ size_t size);
};
//---------------------------------------------------------------------------------------------------------------------