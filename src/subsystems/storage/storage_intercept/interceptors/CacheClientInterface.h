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
// The pointer returned by SI_getLastError() is valid until the next call to SI_getLastError(),
// and is undefined after that.

// Example:
//  const char* error = SI_getLastError();
//  printf("Error: %s\n", error); <- Valid pointer
//  const char* secondError = SI_getLastError();
//  printf("Error: %s\n", error); <- error is now invalid
//  printf("Error: %s\n", secondError); <- secondError is valid
const char* SI_getLastError();

#endif
//---------------------------------------------------------------------------------------------------------------------