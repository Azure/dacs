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