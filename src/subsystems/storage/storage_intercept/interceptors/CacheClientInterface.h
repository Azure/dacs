///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// API interface for Cache client
//---------------------------------------------------------------------------------------------------------------------
#ifndef _CACHE_CLIENT_INTERFACE_H_
#define _CACHE_CLIENT_INTERFACE_H_

#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

// libc APIs
int SI_open(/*[in]*/ const char *pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode, /*[out]*/ char** errorString);
ssize_t SI_pread(/*[in]*/ int fd, /*[in]*/ void* buf, /*[in]*/ size_t nByte, /*[in]*/ off64_t offset, /*[out]*/ char** errorString);
int SI_close(/*[in]*/ int fd, /*[out]*/ char** errorString);
int SI_fstat(/*[in]*/ int fd, /*[in]*/ struct stat *statbuf, /*[out]*/ char** errorString);
// End of libc APIs

#ifdef __cplusplus
}
#endif

#endif
//---------------------------------------------------------------------------------------------------------------------