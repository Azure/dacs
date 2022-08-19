///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
#include "StorageDlsymWrapper.h"
#include <cstdlib>
#include "ContentHandler.h"
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::open(const char* pathname, int flags, mode_t mode)
{
    return ContentHandler::GetInstance().HandleOpen(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::open64(const char* pathname, int flags, mode_t mode)
{
    return ContentHandler::GetInstance().HandleOpen64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::fopen(const char* pathname, const char* mode)
{
    return ContentHandler::GetInstance().HandleFopen(pathname, mode, false);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::fopen64(const char* pathname, const char* mode)
{
    return ContentHandler::GetInstance().HandleFopen(pathname, mode, true);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fileno(void* stream)
{
    return ContentHandler::GetInstance().HandleFileNo(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fclose(void* stream)
{
    return ContentHandler::GetInstance().HandleFClose(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::openat(int dirfd, const char* pathname, int flags, mode_t mode)
{
    return ContentHandler::GetInstance().HandleOpenAt(dirfd, pathname, flags, mode, false);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::openat64(int dirfd, const char* pathname, int flags, mode_t mode)
{
    return ContentHandler::GetInstance().HandleOpenAt(dirfd, pathname, flags, mode, true);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageInterceptorImpl::read(int fd, void* buf, size_t nByte)
{
    return ContentHandler::GetInstance().HandleRead(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageInterceptorImpl::pread(int fd, void* buf, size_t size, off64_t offset)
{
    return ContentHandler::GetInstance().HandlePRead(fd, buf, size, offset);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t StorageInterceptorImpl::lseek64(int fd, off64_t offset, int whence)
{
    return ContentHandler::GetInstance().HandleLseek(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::close(int fd)
{
    return ContentHandler::GetInstance().HandleClose(fd);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::opendir(const char* pathname)
{
    return ContentHandler::GetInstance().HandleOpendir(pathname);;
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::readdir(void* dirp)
{
    return ContentHandler::GetInstance().HandleReaddir(dirp, false);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::readdir64(void* dirp)
{
    return ContentHandler::GetInstance().HandleReaddir(dirp, true);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::closedir(void* dirp)
{
    return ContentHandler::GetInstance().HandleClosedir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::access(const char *pathname, int mode)
{
    return ContentHandler::GetInstance().HandleAccess(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return ContentHandler::GetInstance().HandleXstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return ContentHandler::GetInstance().HandleXstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return ContentHandler::GetInstance().HandleLxstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return ContentHandler::GetInstance().HandleLxstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fxstat(int __ver, int fd, struct stat* statbuf)
{
    return ContentHandler::GetInstance().HandleFxstat(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    return ContentHandler::GetInstance().HandleFxstat64(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::ioctl(int fd, unsigned long request, void* argp)
{
    return ContentHandler::GetInstance().HandleIoctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::isatty(int fd)
{
    return ContentHandler::GetInstance().HandleIsatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::flock(int fd, int operation)
{
    return ContentHandler::GetInstance().HandleFlock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageInterceptorImpl::listxattr(const char* path, char* list, size_t size)
{
    return ContentHandler::GetInstance().HandleListxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------