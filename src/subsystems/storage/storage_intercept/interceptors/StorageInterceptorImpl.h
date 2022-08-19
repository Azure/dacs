///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include <sys/stat.h>
//---------------------------------------------------------------------------------------------------------------------
class StorageInterceptorImpl
{
    public:
        static int open(const char* pathname, int flags, mode_t mode);
        static int open64(const char* pathname, int flags, mode_t mode);

        static int openat(int dirfd, const char *pathname, int flags, mode_t mode);
        static int openat64(int dirfd, const char *pathname, int flags, mode_t mode);

        static void* fopen(const char* pathname, const char* mode);
        static void* fopen64(const char* pathname, const char* mode);
        static int fileno(void* stream);
        static int fclose(void* stream);

        static ssize_t read(int fd, void* buf, size_t size);
        static ssize_t pread(int fd, void* buf, size_t size, off64_t offset);
        static off64_t lseek64(int fd, off64_t offset, int whence);

        static int close(int fd);

        static void* opendir(const char* pathname);
        static void* readdir(void* dirp);
        static void* readdir64(void* dirp);
        static int closedir(void* dirp);

        static int access(const char *pathname, int mode);
        static int xstat(int __ver, const char* pathname, struct stat* statbuf);
        static int xstat64(int __ver, const char* pathname, struct stat64* statbuf);
        static int fxstat(int __ver, int fd, struct stat* statbuf);
        static int fxstat64(int __ver, int fd, struct stat64* statbuf);
        static int lxstat(int __ver, const char* pathname, struct stat* statbuf);
        static int lxstat64(int __ver, const char* pathname, struct stat64* statbuf);
        static ssize_t listxattr(const char* path, char* list, size_t size);

        static int ioctl(int fd, unsigned long request, void* argp);
        static int isatty(int fd);
        static int flock(int fd, int operation);
};
//---------------------------------------------------------------------------------------------------------------------