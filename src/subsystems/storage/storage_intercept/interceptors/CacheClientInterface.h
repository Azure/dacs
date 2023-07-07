///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// API interface for Cache client
//---------------------------------------------------------------------------------------------------------------------
#ifndef _CACHE_CLIENT_INTERFACE_H_
#define _CACHE_CLIENT_INTERFACE_H_

#include <sys/types.h>
#include <sys/stat.h>

// libc APIs
int SI_open(const char *pathname, int flags, mode_t mode);
ssize_t SI_pread(int fd, void* buf, size_t nByte, off64_t offset);
int SI_close(int fd);
int SI_fstat(int fd, struct stat *statbuf);
// End of libc APIs

// SI_getLastError semantics:
// Returns a pointer to a null-terminated string describing the last error that occurred in the calling thread.
// if no error occurred which originated from the cache client, then NULL is returned
// It is upto the caller to free the returned pointer.

char* SI_getLastError();
//---------------------------------------------------------------------------------------------------------------------

#endif
//---------------------------------------------------------------------------------------------------------------------