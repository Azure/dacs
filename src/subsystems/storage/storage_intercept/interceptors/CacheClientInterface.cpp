///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_open(/*[in]*/ const char *pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode, /*[out]*/ char** errorString)
{
    return StorageInterceptorImpl::SI_open(pathname, flags, mode, errorString);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t SI_pread(/*[in]*/ int fd, /*[in]*/ void* buf, /*[in]*/ size_t nByte, /*[in]*/ off64_t offset, /*[out]*/ char** errorString)
{
    return StorageInterceptorImpl::SI_pread(fd, buf, nByte, offset, errorString);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_close(/*[in]*/ int fd, /*[out]*/ char** errorString)
{
    return StorageInterceptorImpl::SI_close(fd, errorString);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int SI_fstat(/*[in]*/ int fd, /*[in]*/ struct stat *statbuf, /*[out]*/ char** errorString)
{
    return StorageInterceptorImpl::SI_fstat(fd, statbuf, errorString);
}
//---------------------------------------------------------------------------------------------------------------------