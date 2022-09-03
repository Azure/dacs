///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open(const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::open(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open64(const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::open64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::openat(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat64(int dirfd, const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::openat64(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* fopen(const char* pathname, const char* mode)
{
    return StorageInterceptorImpl::fopen(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* fopen64(const char* pathname, const char* mode)
{
    return StorageInterceptorImpl::fopen64(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fileno(void* stream)
{
    return StorageInterceptorImpl::fileno(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fclose(void* stream)
{
    return StorageInterceptorImpl::fclose(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t __read_chk(int fd, void* buf, size_t nByte, size_t buflen)
{
    // According to API spec this is same as read() except we should abort if buffer overflow is anticipated
    // Ref: http://refspecs.linux-foundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/libc---read-chk-1.html
    if (nByte > buflen)
    {
        throw "buffer overflow detected";
    }

    return StorageInterceptorImpl::read(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t read(int fd, void* buf, size_t nByte)
{
    return StorageInterceptorImpl::read(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" off64_t lseek64(int fd, off64_t offset, int whence)
{
    return StorageInterceptorImpl::lseek64(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t pread(int fd, void* buf, size_t nByte, off64_t offset)
{
    return StorageInterceptorImpl::pread(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int close(int fd)
{
   return StorageInterceptorImpl::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* opendir(const char *pathname)
{
    return StorageInterceptorImpl::opendir(pathname);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir(void* dirp)
{
    return StorageInterceptorImpl::readdir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir64(void* dirp)
{
    return StorageInterceptorImpl::readdir64(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int closedir(void* dirp)
{
    return StorageInterceptorImpl::closedir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int access(const char *pathname, int mode)
{
    return StorageInterceptorImpl::access(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return StorageInterceptorImpl::xstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return StorageInterceptorImpl::xstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat(int __ver, int fd, struct stat* statbuf)
{
    return StorageInterceptorImpl::fxstat(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    return StorageInterceptorImpl::fxstat64(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return StorageInterceptorImpl::lxstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return StorageInterceptorImpl::lxstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ioctl(int fd, unsigned long request, void* argp)
{
    return StorageInterceptorImpl::ioctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int isatty(int fd)
{
    return StorageInterceptorImpl::isatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int flock(int fd, int operation)
{
    return StorageInterceptorImpl::flock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t listxattr(const char* path, char* list, size_t size)
{
    return StorageInterceptorImpl::listxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------