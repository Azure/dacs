/// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// API interface for Cache client
//---------------------------------------------------------------------------------------------------------------------
#ifndef _CACHE_CLIENT_INTERFACE_H_
#define _CACHE_CLIENT_INTERFACE_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef __cplusplus
extern "C" {
#endif

// libc APIs
int SI_open(/*[in]*/ const char *pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode, /*[out]*/ char** errorString);
ssize_t SI_pread(/*[in]*/ int fd, /*[in]*/ void* buf, /*[in]*/ size_t nByte, /*[in]*/ off64_t offset, /*[out]*/ char** errorString);
int SI_close(/*[in]*/ int fd, /*[out]*/ char** errorString);
int SI_fstat(/*[in]*/ int fd, /*[in]*/ struct stat *statbuf, /*[out]*/ char** errorString);
// Directory enumeration. SI_readdir returns the next entry, NULL at end-of-dir
// (with *errorString == NULL) or NULL on error (with *errorString set).
void* SI_opendir(/*[in]*/ const char *pathname, /*[out]*/ char** errorString);
struct dirent* SI_readdir(/*[in]*/ void* dirp, /*[out]*/ char** errorString);
int SI_closedir(/*[in]*/ void* dirp, /*[out]*/ char** errorString);
// End of libc APIs

#ifdef __cplusplus
}
#endif

#endif
//---------------------------------------------------------------------------------------------------------------------