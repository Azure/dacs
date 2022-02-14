///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// The StorageDlsymWrapper provides the original libc implementation of the intercepted libc calls, in case the intercepted
// libc calls need to fallback to their original counterpart. It loads the "next" libc call in the load order.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageDlsymWrapper.h"
#include <dlfcn.h>
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::open(const char* pathname, int flags, mode_t mode=0777)
{
    Original_open m_originalOpen = (Original_open)dlsym(RTLD_NEXT, "open");
    return m_originalOpen(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::open64(const char* pathname, int flags, mode_t mode=0777)
{
    Original_open64 m_originalOpen64 = (Original_open64)dlsym(RTLD_NEXT, "open64");
    return m_originalOpen64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::close(int fd)
{
    Original_close m_originalClose = (Original_close)dlsym(RTLD_NEXT, "close");
    return m_originalClose(fd);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageDlsymWrapper::read(int fd, void *buf, size_t nByte)
{
    Original_read m_originalRead = (Original_read)dlsym(RTLD_NEXT, "read");
    return m_originalRead(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageDlsymWrapper::pread(int fd, void *buf, size_t nByte, off64_t offset)
{
    Original_pread m_originalPRead = (Original_pread)dlsym(RTLD_NEXT, "pread");
    return m_originalPRead(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t StorageDlsymWrapper::lseek64(int fd, off64_t offset, int whence)
{
    Original_lseek64 m_OriginalLseek64 = (Original_lseek64)dlsym(RTLD_NEXT, "lseek64");
    return m_OriginalLseek64(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageDlsymWrapper::opendir(const char* pathname)
{
    Original_opendir m_OriginalOpendir = (Original_opendir)dlsym(RTLD_NEXT, "opendir");
    return m_OriginalOpendir(pathname);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageDlsymWrapper::readdir64(void* dirp)
{
    Original_readdir64 m_OriginalReaddir64 = (Original_readdir64)dlsym(RTLD_NEXT, "readdir64");
    return m_OriginalReaddir64(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::closedir(void* dirp)
{
    Original_closedir m_OriginalClosedir = (Original_closedir)dlsym(RTLD_NEXT, "closedir");
    return m_OriginalClosedir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    Original_xstat m_OriginalXstat = (Original_xstat)dlsym(RTLD_NEXT, "__xstat");
    return m_OriginalXstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    Original_xstat64 m_OriginalXstat64 = (Original_xstat64)dlsym(RTLD_NEXT, "__xstat64");
    return m_OriginalXstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    Original_lxstat m_OriginalLxstat = (Original_lxstat)dlsym(RTLD_NEXT, "__lxstat");
    return m_OriginalLxstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    Original_lxstat64 m_OriginalLxstat64 = (Original_lxstat64)dlsym(RTLD_NEXT, "__lxstat64");
    return m_OriginalLxstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::fxstat(int __ver, int fd, struct stat* statbuf)
{
    Original_fxstat m_OriginalFxstat = (Original_fxstat)dlsym(RTLD_NEXT, "__fxstat");
    return m_OriginalFxstat(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    Original_fxstat64 m_OriginalFxstat64 = (Original_fxstat64)dlsym(RTLD_NEXT, "__fxstat64");
    return m_OriginalFxstat64(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::ioctl(int fd, unsigned long request, void* argp)
{
    Original_ioctl m_OriginalIoctl = (Original_ioctl)dlsym(RTLD_NEXT, "ioctl");
    return m_OriginalIoctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::isatty(int fd)
{
    Original_isatty m_OriginalIsatty = (Original_isatty)dlsym(RTLD_NEXT, "isatty");
    return m_OriginalIsatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageDlsymWrapper::flock(int fd, int operation)
{
    Original_flock m_OriginalFlock = (Original_flock)dlsym(RTLD_NEXT, "flock");
    return m_OriginalFlock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageDlsymWrapper::listxattr(const char* path, char* list, size_t size)
{
    Original_listxattr m_OriginalListxattr = (Original_listxattr)dlsym(RTLD_NEXT, "listxattr");
    return m_OriginalListxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------