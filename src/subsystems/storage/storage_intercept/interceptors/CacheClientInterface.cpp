///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_open(const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::open(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t SI_pread(int fd, void* buf, size_t nByte, off64_t offset)
{
    return StorageInterceptorImpl::pread(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_close(int fd)
{
   return StorageInterceptorImpl::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_fstat(int fd, struct stat *statbuf)
{
    return StorageInterceptorImpl::fstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" char* SI_getLastError()
{
    return StorageInterceptorImpl::getLastError();
}
//---------------------------------------------------------------------------------------------------------------------