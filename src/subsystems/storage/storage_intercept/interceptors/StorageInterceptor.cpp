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