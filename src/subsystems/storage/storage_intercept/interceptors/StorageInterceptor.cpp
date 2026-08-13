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
#include "../remote_client/RemoteClientException.h"
#include <errno.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
//---------------------------------------------------------------------------------------------------------------------
// Helper function to map exception messages to appropriate errno values
// Returns the appropriate errno based on the error message content
static int mapExceptionToErrno(const char* errorMsg) {
    if (errorMsg == nullptr) {
        return EIO;
    }
    
    // Authorization/Permission errors -> EPERM or EACCES
    if (strstr(errorMsg, "AuthorizationPermissionMismatch") != nullptr ||
        strstr(errorMsg, "AuthorizationFailed") != nullptr ||
        strstr(errorMsg, "AuthorizationFailure") != nullptr ||
        strstr(errorMsg, "InsufficientAccountPermissions") != nullptr ||
        strstr(errorMsg, "AccountIsDisabled") != nullptr) {
        return EPERM;
    }
    
    // Access denied -> EACCES
    if (strstr(errorMsg, "AccessDenied") != nullptr ||
        strstr(errorMsg, "Forbidden") != nullptr ||
        strstr(errorMsg, "403") != nullptr) {
        return EACCES;
    }
    
    // Not found errors -> ENOENT
    if (strstr(errorMsg, "BlobNotFound") != nullptr ||
        strstr(errorMsg, "ContainerNotFound") != nullptr ||
        strstr(errorMsg, "ResourceNotFound") != nullptr ||
        strstr(errorMsg, "PathNotFound") != nullptr ||
        strstr(errorMsg, "FileNotFound") != nullptr ||
        strstr(errorMsg, "404") != nullptr ||
        strstr(errorMsg, "NotFound") != nullptr) {
        return ENOENT;
    }
    
    // Already exists -> EEXIST
    if (strstr(errorMsg, "BlobAlreadyExists") != nullptr ||
        strstr(errorMsg, "ContainerAlreadyExists") != nullptr ||
        strstr(errorMsg, "ResourceAlreadyExists") != nullptr ||
        strstr(errorMsg, "PathAlreadyExists") != nullptr ||
        strstr(errorMsg, "409") != nullptr) {
        return EEXIST;
    }
    
    // Conflict/busy errors -> EBUSY
    if (strstr(errorMsg, "LeaseAlreadyPresent") != nullptr ||
        strstr(errorMsg, "LeaseIdMissing") != nullptr ||
        strstr(errorMsg, "LeaseConflict") != nullptr ||
        strstr(errorMsg, "OperationNotAllowedOnRootBlob") != nullptr) {
        return EBUSY;
    }
    
    // Timeout errors -> ETIMEDOUT
    if (strstr(errorMsg, "Timeout") != nullptr ||
        strstr(errorMsg, "OperationTimedOut") != nullptr ||
        strstr(errorMsg, "ServerTimeout") != nullptr ||
        strstr(errorMsg, "ClientTimeout") != nullptr) {
        return ETIMEDOUT;
    }
    
    // Connection/network errors -> ECONNREFUSED, ENETUNREACH, EHOSTUNREACH
    if (strstr(errorMsg, "ConnectionRefused") != nullptr ||
        strstr(errorMsg, "ConnectionReset") != nullptr ||
        strstr(errorMsg, "Connection refused") != nullptr) {
        return ECONNREFUSED;
    }
    if (strstr(errorMsg, "NetworkUnreachable") != nullptr ||
        strstr(errorMsg, "Network is unreachable") != nullptr) {
        return ENETUNREACH;
    }
    if (strstr(errorMsg, "HostUnreachable") != nullptr ||
        strstr(errorMsg, "No route to host") != nullptr) {
        return EHOSTUNREACH;
    }
    if (strstr(errorMsg, "ConnectionError") != nullptr ||
        strstr(errorMsg, "SocketError") != nullptr ||
        strstr(errorMsg, "NetworkError") != nullptr) {
        return ECONNRESET;
    }
    
    // Service unavailable -> EAGAIN (retry)
    if (strstr(errorMsg, "ServiceUnavailable") != nullptr ||
        strstr(errorMsg, "ServerBusy") != nullptr ||
        strstr(errorMsg, "503") != nullptr ||
        strstr(errorMsg, "InternalError") != nullptr ||
        strstr(errorMsg, "500") != nullptr) {
        return EAGAIN;
    }
    
    // Quota/space errors -> ENOSPC or EDQUOT
    if (strstr(errorMsg, "QuotaExceeded") != nullptr ||
        strstr(errorMsg, "AccountQuotaExceeded") != nullptr ||
        strstr(errorMsg, "ShareQuotaExceeded") != nullptr) {
        return EDQUOT;
    }
    if (strstr(errorMsg, "MaxBlobSizeExceeded") != nullptr ||
        strstr(errorMsg, "BlockCountExceeded") != nullptr ||
        strstr(errorMsg, "FileTooLarge") != nullptr) {
        return EFBIG;
    }
    
    // Invalid input errors -> EINVAL
    if (strstr(errorMsg, "InvalidInput") != nullptr ||
        strstr(errorMsg, "InvalidParameter") != nullptr ||
        strstr(errorMsg, "InvalidRange") != nullptr ||
        strstr(errorMsg, "InvalidHeaderValue") != nullptr ||
        strstr(errorMsg, "InvalidQueryParameter") != nullptr ||
        strstr(errorMsg, "OutOfRangeInput") != nullptr ||
        strstr(errorMsg, "400") != nullptr) {
        return EINVAL;
    }
    
    // Name too long -> ENAMETOOLONG
    if (strstr(errorMsg, "InvalidBlobName") != nullptr ||
        strstr(errorMsg, "InvalidContainerName") != nullptr ||
        strstr(errorMsg, "NameTooLong") != nullptr) {
        return ENAMETOOLONG;
    }
    
    // Read-only errors -> EROFS
    if (strstr(errorMsg, "ReadOnlyAttribute") != nullptr ||
        strstr(errorMsg, "ConditionNotMet") != nullptr) {
        return EROFS;
    }
    
    // Operation not supported -> ENOTSUP
    if (strstr(errorMsg, "UnsupportedOperation") != nullptr ||
        strstr(errorMsg, "NotImplemented") != nullptr ||
        strstr(errorMsg, "FeatureNotSupported") != nullptr ||
        strstr(errorMsg, "501") != nullptr) {
        return ENOTSUP;
    }
    
    // Is a directory when file expected -> EISDIR
    if (strstr(errorMsg, "PathIsDirectory") != nullptr ||
        strstr(errorMsg, "IsADirectory") != nullptr) {
        return EISDIR;
    }
    
    // Is a file when directory expected -> ENOTDIR
    if (strstr(errorMsg, "PathIsNotDirectory") != nullptr ||
        strstr(errorMsg, "NotADirectory") != nullptr ||
        strstr(errorMsg, "ParentNotFound") != nullptr) {
        return ENOTDIR;
    }
    
    // Directory not empty -> ENOTEMPTY
    if (strstr(errorMsg, "DirectoryNotEmpty") != nullptr) {
        return ENOTEMPTY;
    }
    
    // Default to EIO for unknown errors
    return EIO;
}
//---------------------------------------------------------------------------------------------------------------------
// Helper macro for exception-safe wrappers
// This catches C++ exceptions and converts them to errno-based error returns
// This is critical for Python/ctypes interop - C++ exceptions cannot cross the boundary
#define EXCEPTION_SAFE_WRAPPER(return_type, error_return, body) \
    try { \
        body \
    } catch (const RemoteClientException& e) { \
        const char* msg = e.what(); \
        fprintf(stderr, "StorageInterceptor: RemoteClientException: %s\n", msg); \
        errno = mapExceptionToErrno(msg); \
        return error_return; \
    } catch (const std::exception& e) { \
        const char* msg = e.what(); \
        fprintf(stderr, "StorageInterceptor: std::exception: %s\n", msg); \
        errno = mapExceptionToErrno(msg); \
        return error_return; \
    } catch (...) { \
        fprintf(stderr, "StorageInterceptor: Unknown exception\n"); \
        errno = EIO; \
        return error_return; \
    }
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open(const char *pathname, int flags, mode_t mode)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::open(pathname, flags, mode);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open64(const char *pathname, int flags, mode_t mode)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::open64(pathname, flags, mode);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::openat(dirfd, pathname, flags, mode);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat64(int dirfd, const char *pathname, int flags, mode_t mode)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::openat64(dirfd, pathname, flags, mode);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" FILE* fopen(const char* pathname, const char* mode)
{
    EXCEPTION_SAFE_WRAPPER(FILE*, nullptr,
        return static_cast<FILE*>(StorageInterceptorImpl::fopen(pathname, mode));
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" FILE* fopen64(const char* pathname, const char* mode)
{
    EXCEPTION_SAFE_WRAPPER(FILE*, nullptr,
        return static_cast<FILE*>(StorageInterceptorImpl::fopen64(pathname, mode));
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fileno(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fileno(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fclose(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fclose(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t __read_chk(int fd, void* buf, size_t nByte, size_t buflen)
{
    // According to API spec this is same as read() except we should abort if buffer overflow is anticipated
    // Ref: http://refspecs.linux-foundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/libc---read-chk-1.html
    if (nByte > buflen)
    {
        fprintf(stderr, "StorageInterceptor: __read_chk buffer overflow detected\n");
        errno = EOVERFLOW;
        return -1;
    }

    EXCEPTION_SAFE_WRAPPER(ssize_t, -1,
        return StorageInterceptorImpl::read(fd, buf, nByte);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t read(int fd, void* buf, size_t nByte)
{
    EXCEPTION_SAFE_WRAPPER(ssize_t, -1,
        return StorageInterceptorImpl::read(fd, buf, nByte);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" off64_t lseek(int fd, off64_t offset, int whence)
{
    EXCEPTION_SAFE_WRAPPER(off64_t, -1,
        return StorageInterceptorImpl::lseek(fd, offset, whence);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" off64_t lseek64(int fd, off64_t offset, int whence)
{
    EXCEPTION_SAFE_WRAPPER(off64_t, -1,
        return StorageInterceptorImpl::lseek64(fd, offset, whence);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t pread(int fd, void* buf, size_t nByte, off64_t offset)
{
    EXCEPTION_SAFE_WRAPPER(ssize_t, -1,
        return StorageInterceptorImpl::pread(fd, buf, nByte, offset);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t pwrite(int fd, const void* buf, size_t nByte, off64_t offset)
{
    EXCEPTION_SAFE_WRAPPER(ssize_t, -1,
        return StorageInterceptorImpl::pwrite(fd, buf, nByte, offset);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int close(int fd)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::close(fd);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* opendir(const char *pathname)
{
    EXCEPTION_SAFE_WRAPPER(void*, nullptr,
        return StorageInterceptorImpl::opendir(pathname);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir(void* dirp)
{
    EXCEPTION_SAFE_WRAPPER(void*, nullptr,
        return StorageInterceptorImpl::readdir(dirp);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir64(void* dirp)
{
    EXCEPTION_SAFE_WRAPPER(void*, nullptr,
        return StorageInterceptorImpl::readdir64(dirp);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int closedir(void* dirp)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::closedir(dirp);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int access(const char *pathname, int mode)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::access(pathname, mode);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int stat(const char* pathname, struct stat* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::stat(pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int stat64(const char* pathname, struct stat64* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::stat64(pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::xstat(__ver, pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::xstat64(__ver, pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fstat(int fd, struct stat *statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fstat(fd, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fstat64(int fd, struct stat64 *statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fstat64(fd, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat(int __ver, int fd, struct stat* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fxstat(__ver, fd, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fxstat64(__ver, fd, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::lxstat(__ver, pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::lxstat64(__ver, pathname, statbuf);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ioctl(int fd, unsigned long request, void* argp)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::ioctl(fd, request, argp);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int isatty(int fd)
{
    EXCEPTION_SAFE_WRAPPER(int, 0,
        return StorageInterceptorImpl::isatty(fd);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int flock(int fd, int operation)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::flock(fd, operation);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t listxattr(const char* path, char* list, size_t size)
{
    EXCEPTION_SAFE_WRAPPER(ssize_t, -1,
        return StorageInterceptorImpl::listxattr(path, list, size);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void clearerr(FILE* stream)
{
    try {
        return StorageInterceptorImpl::clearerr(stream);
    } catch (...) {
        // void return - nothing to do
    }
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int feof(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, 0,
        return StorageInterceptorImpl::feof(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ferror(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, 1,
        return StorageInterceptorImpl::ferror(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fflush(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::fflush(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetc(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::fgetc(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetpos(FILE* stream, fpos_t* pos)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fgetpos(stream, pos);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetpos64(FILE* stream, fpos64_t* pos)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fgetpos64(stream, pos);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" char* fgets(char* s, int size, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(char*, nullptr,
        return StorageInterceptorImpl::fgets(s, size, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fprintf(FILE* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = -1;
    try {
        ret = StorageInterceptorImpl::vfprintf(stream, format, args);
    } catch (...) {
        errno = EIO;
    }
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fpurge(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fpurge(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fputc(int c, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::fputc(c, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fputs(const char* s, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::fputs(s, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(size_t, 0,
        return StorageInterceptorImpl::fread(ptr, size, nmemb, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" FILE* freopen(const char* pathname, const char* mode, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(FILE*, nullptr,
        return static_cast<FILE*>(StorageInterceptorImpl::freopen(pathname, mode, stream));
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" FILE* freopen64(const char* pathname, const char* mode, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(FILE*, nullptr,
        return static_cast<FILE*>(StorageInterceptorImpl::freopen64(pathname, mode, stream));
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fscanf(FILE* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = EOF;
    try {
        ret = StorageInterceptorImpl::vfscanf(stream, format, args);
    } catch (...) {
        errno = EIO;
    }
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fseek(FILE* stream, long offset, int whence)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fseek(stream, offset, whence);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fsetpos(FILE* stream, const fpos_t* pos)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fsetpos(stream, pos);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fsetpos64(FILE* stream, const fpos64_t* pos)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::fsetpos64(stream, pos);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" long ftell(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(long, -1,
        return StorageInterceptorImpl::ftell(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(size_t, 0,
        return StorageInterceptorImpl::fwrite(ptr, size, nmemb, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int getc(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::getc(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int getw(FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::getw(stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int putc(int c, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::putc(c, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int putw(int w, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::putw(w, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void rewind(FILE* stream)
{
    try {
        return StorageInterceptorImpl::rewind(stream);
    } catch (...) {
        // void return - nothing to do
    }
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setbuf(FILE* stream, char* buf)
{
    try {
        return StorageInterceptorImpl::setbuf(stream, buf);
    } catch (...) {
        // void return - nothing to do
    }
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setbuffer(FILE* stream, char* buf, size_t size)
{
    try {
        return StorageInterceptorImpl::setbuffer(stream,buf, size);
    } catch (...) {
        // void return - nothing to do
    }
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setlinebuf(FILE* stream)
{
    try {
        return StorageInterceptorImpl::setlinebuf(stream);
    } catch (...) {
        // void return - nothing to do
    }
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int setvbuf(FILE* stream, char* buf, int mode, size_t size)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::setvbuf(stream, buf, mode, size);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ungetc(int c, FILE* stream)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::ungetc(c, stream);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int vfprintf(FILE* stream, const char* format, va_list ap)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::vfprintf(stream, format, ap);
    )
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int vfscanf(FILE* stream, const char* format, va_list ap)
{
    EXCEPTION_SAFE_WRAPPER(int, EOF,
        return StorageInterceptorImpl::vfscanf(stream, format, ap);
    )
}
//---------------------------------------------------------------------------------------------------------------------
// SI_* functions already have exception handling in StorageInterceptorImpl
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
// Architecture-agnostic stat API for Python binding.
// Uses FileStat struct with fixed-width types whose layout is identical on all architectures.
//---------------------------------------------------------------------------------------------------------------------
extern "C" int dacs_stat(const char* pathname, struct FileStat* out)
{
    EXCEPTION_SAFE_WRAPPER(int, -1,
        return StorageInterceptorImpl::dacs_stat(pathname, out);
    )
}
//---------------------------------------------------------------------------------------------------------------------