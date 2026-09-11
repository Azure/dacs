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
#include "StorageLibcApiWrapper.h"
#include "ContentHandler.h"
#include "RemoteClientException.h"
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <string>
#include <typeinfo>
//---------------------------------------------------------------------------------------------------------------------
// Build a heap-allocated (malloc'd, freed by the caller) description of the
// C++ exception currently in flight. Used by the extern "C" SI_* catch(...)
// handlers so an otherwise-opaque foreign exception surfaces its real
// (demangled) type at the FFI boundary instead of a generic "unknown C++
// exception" string. Must never itself throw.
static char* DescribeCurrentFfiException(const char* fnName) noexcept
{
    const char* safeFnName = (fnName != nullptr && fnName[0] != '\0') ? fnName : "SI_*";

    char msg[512];
    msg[0] = '\0';

    auto Append = [&](const char* s) noexcept
    {
        if(s == nullptr)
        {
            return;
        }
        const size_t cur = std::strlen(msg);
        if(cur >= sizeof(msg) - 1)
        {
            return;
        }
        size_t rem = sizeof(msg) - cur - 1;
        size_t len = std::strlen(s);
        if(len > rem)
        {
            len = rem;
        }
        std::memcpy(msg + cur, s, len);
        msg[cur + len] = '\0';
    };

    Append(safeFnName);
    Append(": unhandled C++ exception at FFI boundary");
    if(const std::type_info* t = abi::__cxa_current_exception_type())
    {
        int status = 0;
        char* demangled = abi::__cxa_demangle(t->name(), nullptr, nullptr, &status);
        Append(" (type ");
        Append((status == 0 && demangled != nullptr) ? demangled : t->name());
        Append(")");
        ::free(demangled);
    }

    // Never throw: on allocation failure fall back to a static message, then to
    // an allocated empty string the Rust caller can still free.
    if(char* out = ::strdup(msg))
    {
        return out;
    }
    static const char kFallback[] =
        "unhandled C++ exception at FFI boundary (failed to allocate error string)";
    if(char* out = ::strdup(kFallback))
    {
        return out;
    }
    char* out = static_cast<char*>(::malloc(1));
    if(out != nullptr)
    {
        out[0] = '\0';
    }
    return out;
}
//---------------------------------------------------------------------------------------------------------------------
// Shared FFI-boundary catch chain for every SI_* entry point below. These
// functions are invoked from generic Rust callers; a C++ exception must NEVER
// unwind across the extern "C" boundary, because Rust's catch_unwind treats a
// foreign (C++) exception as unrecoverable and calls abort() ("Rust cannot
// catch foreign exceptions, aborting"). Pair with a `try { ... }` that sets
// *errorString on its own (expected) failure path; this macro only covers the
// (exceptional) case of the try block throwing -- including bare
// std::string/const char* that some call paths historically threw.
#define SI_CATCH_FFI_BOUNDARY(errorString, fnName, errorValue) \
    catch(const RemoteClientException& e) \
    { \
        *(errorString) = ::strdup(e.what()); \
        errno = EIO; \
        return (errorValue); \
    } \
    catch(const std::exception& e) \
    { \
        *(errorString) = ::strdup(e.what()); \
        errno = EIO; \
        return (errorValue); \
    } \
    catch(const std::string& e) \
    { \
        *(errorString) = ::strdup(e.c_str()); \
        errno = EIO; \
        return (errorValue); \
    } \
    catch(const char* e) \
    { \
        *(errorString) = ::strdup(e); \
        errno = EIO; \
        return (errorValue); \
    } \
    catch(...) \
    { \
        *(errorString) = DescribeCurrentFfiException(fnName); \
        errno = EIO; \
        return (errorValue); \
    }
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
off64_t StorageInterceptorImpl::lseek(int fd, off64_t offset, int whence)
{
    return ContentHandler::GetInstance().HandleLseek(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t StorageInterceptorImpl::lseek64(int fd, off64_t offset, int whence)
{
    return ContentHandler::GetInstance().HandleLseek(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageInterceptorImpl::pwrite(int fd, const void* buf, size_t size, off64_t offset)
{
    return ContentHandler::GetInstance().HandlePWrite(fd, buf, size, offset);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::close(int fd)
{
    return ContentHandler::GetInstance().HandleClose(fd);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::opendir(const char* pathname)
{
    return ContentHandler::GetInstance().HandleOpendir(pathname);
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
int StorageInterceptorImpl::stat(const char* pathname, struct stat* statbuf)
{
    return ContentHandler::GetInstance().HandleStat(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::stat64(const char* pathname, struct stat64* statbuf)
{
    return ContentHandler::GetInstance().HandleStat64(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::dacs_stat(const char* pathname, struct FileStat* statbuf)
{
    return ContentHandler::GetInstance().HandleDACSStat(pathname, statbuf);
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
int StorageInterceptorImpl::fstat(int fd, struct stat* statbuf)
{
    return ContentHandler::GetInstance().HandleFstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fstat64(int fd, struct stat64* statbuf)
{
    return ContentHandler::GetInstance().HandleFstat64(fd, statbuf);
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
void StorageInterceptorImpl::clearerr(void* stream)
{
    return ContentHandler::GetInstance().Handleclearerr(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::feof(void* stream)
{
    return ContentHandler::GetInstance().Handlefeof(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::ferror(void* stream)
{
    return ContentHandler::GetInstance().Handleferror(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fflush(void* stream)
{
    return ContentHandler::GetInstance().Handlefflush(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fgetc(void* stream)
{
    return ContentHandler::GetInstance().Handlefgetc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fgetpos(void* stream, void* pos)
{
    return ContentHandler::GetInstance().Handlefgetpos(stream, pos, false);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fgetpos64(void* stream, void* pos)
{
    return ContentHandler::GetInstance().Handlefgetpos(stream, pos, true);
}
//---------------------------------------------------------------------------------------------------------------------
char* StorageInterceptorImpl::fgets(char* s, int size, void* stream)
{
    return ContentHandler::GetInstance().Handlefgets(s, size, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fpurge(void* stream)
{
    return ContentHandler::GetInstance().Handlefpurge(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fputc(int c, void* stream)
{
    return ContentHandler::GetInstance().Handlefputc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fputs(const char* s, void* stream)
{
    return ContentHandler::GetInstance().Handlefputs(s, stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageInterceptorImpl::fread(void* ptr, size_t size, size_t nmemb, void* stream)
{
    return ContentHandler::GetInstance().Handlefread(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::freopen(const char* pathname, const char* mode, void* stream)
{
    return ContentHandler::GetInstance().Handlefreopen(pathname, mode, stream, false);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::freopen64(const char* pathname, const char* mode, void* stream)
{
    return ContentHandler::GetInstance().Handlefreopen(pathname, mode, stream, true);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fseek(void* stream, long offset, int whence)
{
    return ContentHandler::GetInstance().Handlefseek(stream, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fsetpos(void* stream, const void* pos)
{
    return ContentHandler::GetInstance().Handlefsetpos(stream, pos, false);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::fsetpos64(void* stream, const void* pos)
{
    return ContentHandler::GetInstance().Handlefsetpos(stream, pos, true);
}
//---------------------------------------------------------------------------------------------------------------------
long StorageInterceptorImpl::ftell(void* stream)
{
    return ContentHandler::GetInstance().Handleftell(stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageInterceptorImpl::fwrite(const void* ptr, size_t size, size_t nmemb, void* stream)
{
    return ContentHandler::GetInstance().Handlefwrite(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::getc(void* stream)
{
    return ContentHandler::GetInstance().Handlegetc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::getw(void* stream)
{
    return ContentHandler::GetInstance().Handlegetw(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::putc(int c, void* stream)
{
    return ContentHandler::GetInstance().Handleputc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::putw(int w, void* stream)
{
    return ContentHandler::GetInstance().Handleputw(w, stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptorImpl::rewind(void* stream)
{
    return ContentHandler::GetInstance().Handlerewind(stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptorImpl::setbuf(void* stream, char* buf)
{
    return ContentHandler::GetInstance().Handlesetbuf(stream, buf);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptorImpl::setbuffer(void* stream, char* buf, size_t size)
{
    return ContentHandler::GetInstance().Handlesetbuffer(stream, buf, size);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptorImpl::setlinebuf(void* stream)
{
    return ContentHandler::GetInstance().Handlesetlinebuf(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::setvbuf(void* stream, char* buf, int mode, size_t size)
{
    return ContentHandler::GetInstance().Handlesetvbuf(stream, buf, mode, size);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::ungetc(int c, void* stream)
{
    return ContentHandler::GetInstance().Handleungetc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::vfprintf(void* stream, const char* format, va_list ap)
{
    return ContentHandler::GetInstance().Handlevfprintf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::vfscanf(void* stream, const char* format, va_list ap)
{
    return ContentHandler::GetInstance().Handlevfscanf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::SI_open(const char* pathname, int flags, int mode, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return -1;
    }

    try
    {
        int fd = ContentHandler::GetInstance().HandleOpen(pathname, flags, mode);
        if(fd < 0)
        {
            *errorString = ::strdup(::strerror(errno));
        }
        else
        {
            *errorString = nullptr;
        }

        return fd;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_open", -1)
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageInterceptorImpl::SI_pread(int fd, void* buf, size_t count, off64_t offset, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return -1;
    }

    try
    {
        // ssize_t, not int: HandlePRead returns ssize_t,
        // and narrowing to int truncated/mishandled reads over INT_MAX bytes.
        ssize_t ret = ContentHandler::GetInstance().HandlePRead(fd, buf, count, offset);
        if(ret < 0)
        {
            *errorString = ::strdup(::strerror(errno));
        }
        else
        {
            *errorString = nullptr;
        }

        return ret;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_pread", -1)
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::SI_close(int fd, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return -1;
    }

    try
    {
        int ret = ContentHandler::GetInstance().HandleClose(fd);
        if(ret < 0)
        {
            *errorString = ::strdup(::strerror(errno));
        }
        else
        {
            *errorString = nullptr;
        }

        return ret;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_close", -1)
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::SI_fstat(int fd, struct stat* statbuf, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return -1;
    }

    try
    {
        int ret = ContentHandler::GetInstance().HandleFstat(fd, statbuf);
        if(ret < 0)
        {
            *errorString = ::strdup(::strerror(errno));
        }
        else
        {
            *errorString = nullptr;
        }

        return ret;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_fstat", -1)
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::SI_opendir(const char* pathname, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return nullptr;
    }

    try
    {
        void* dirp = opendir(pathname);
        *errorString = (dirp == nullptr) ? ::strdup(::strerror(errno)) : nullptr;
        return dirp;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_opendir", nullptr)
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageInterceptorImpl::SI_readdir(void* dirp, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return nullptr;
    }

    try
    {
        // readdir() returns NULL both at end-of-directory and on error; the two
        // are distinguished by errno (unchanged at end-of-dir, set on error).
        errno = 0;
        void* entry = readdir(dirp);
        *errorString = (entry == nullptr && errno != 0) ? ::strdup(::strerror(errno)) : nullptr;
        return entry;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_readdir", nullptr)
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorImpl::SI_closedir(void* dirp, char** errorString)
{
    if(errorString == nullptr)
    {
        errno = EIO;
        return -1;
    }

    try
    {
        int ret = closedir(dirp);
        *errorString = (ret < 0) ? ::strdup(::strerror(errno)) : nullptr;
        return ret;
    }
    SI_CATCH_FFI_BOUNDARY(errorString, "SI_closedir", -1)
}
//---------------------------------------------------------------------------------------------------------------------