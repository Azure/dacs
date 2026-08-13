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

// This implementation allows the caller to directly access the libc APIs
// this is useful for the DACSInterface library which is directly linked with the target application
// in which case relying on RTLD_NEXT and load ordering may be unreliable
//---------------------------------------------------------------------------------------------------------------------
#include "StorageLibcApiWrapper.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::open(const char* pathname, int flags, mode_t mode=0777)
{
    return ::open(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::open64(const char* pathname, int flags, mode_t mode=0777)
{
    return ::open64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::openat(int dirfd, const char* pathname, int flags, mode_t mode)
{
    return ::openat(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::openat64(int dirfd, const char* pathname, int flags, mode_t mode)
{
    return ::openat64(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::fopen(const char* pathname, const char* mode)
{
    return ::fopen(pathname,  mode);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::fopen64(const char* pathname, const char* mode)
{
    return ::fopen64(pathname,  mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fileno(void* stream)
{
    return ::fileno((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fclose(void* stream)
{
    return ::fclose((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::close(int fd)
{
    return ::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::read(int fd, void *buf, size_t nByte)
{
    return ::read(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::pread(int fd, void *buf, size_t nByte, off64_t offset)
{
    return ::pread(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::write(int fd, const void *buf, size_t nByte)
{
    return ::write(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::pwrite(int fd, const void *buf, size_t nByte, off64_t offset)
{
    return ::pwrite(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t StorageLibcApiWrapper::lseek64(int fd, off64_t offset, int whence)
{
    return ::lseek64(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::opendir(const char* pathname)
{
    return ::opendir(pathname);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::readdir(void* dirp)
{
    return ::readdir((DIR*)dirp);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::readdir64(void* dirp)
{
    return ::readdir64((DIR*)dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::closedir(void* dirp)
{
    return ::closedir((DIR*)dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::access(const char* pathname, int mode)
{
    return ::access(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::stat(const char* pathname, struct stat* statbuf)
{
    return ::stat(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::stat64(const char* pathname, struct stat64* statbuf)
{
    return ::stat64(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return ::stat(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return ::stat64(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return ::lstat(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return ::lstat64(pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fstat(int fd, struct stat* statbuf)
{
    return ::fstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fstat64(int fd, struct stat64* statbuf)
{
    return ::fstat64(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fxstat(int __ver, int fd, struct stat* statbuf)
{
    return ::fstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    return ::fstat64(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ioctl(int fd, unsigned long request, void* argp)
{
    return ::ioctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::isatty(int fd)
{
    return ::isatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::flock(int fd, int operation)
{
    return ::flock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::listxattr(const char* path, char* list, size_t size)
{
    return ::listxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::clearerr(void* stream)
{
    return ::clearerr((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::feof(void* stream)
{
    return ::feof((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ferror(void* stream)
{
    return ::ferror((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fflush(void* stream)
{
    return ::fflush((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetc(void* stream)
{
    return ::fgetc((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetpos(void* stream, void* pos)
{
    return ::fgetpos((FILE*)stream, (fpos_t*)pos);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetpos64(void* stream, void* pos)
{
    return ::fgetpos64((FILE*)stream, (fpos64_t*)pos);
}
//---------------------------------------------------------------------------------------------------------------------
char* StorageLibcApiWrapper::fgets(char* s, int size, void* stream)
{
    return ::fgets(s, size, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fprintf(void* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = ::vfprintf((FILE*)stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fpurge(void* stream)
{
    // fpurge is unsupported on linux, using the replacement function instead
    ::__fpurge((FILE*)stream);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fputc(int c, void* stream)
{
    return ::fputc(c, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fputs(const char* s, void* stream)
{
    return ::fputs(s, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageLibcApiWrapper::fread(void* ptr, size_t size, size_t nmemb, void* stream)
{
    return ::fread(ptr, size, nmemb, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::freopen(const char* pathname, const char* mode, void* stream)
{
    return ::freopen(pathname, mode, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::freopen64(const char* pathname, const char* mode, void* stream)
{
    return ::freopen64(pathname, mode, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fscanf(void* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = ::vfscanf((FILE*)stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fseek(void* stream, long offset, int whence)
{
    return ::fseek((FILE*)stream, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fsetpos(void* stream, const void* pos)
{
    return ::fsetpos((FILE*)stream, (const fpos_t*)pos);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fsetpos64(void* stream, const void* pos)
{
    return ::fsetpos64((FILE*)stream, (const fpos64_t*)pos);
}
//---------------------------------------------------------------------------------------------------------------------
long StorageLibcApiWrapper::ftell(void* stream)
{
    return ::ftell((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageLibcApiWrapper::fwrite(const void* ptr, size_t size, size_t nmemb, void* stream)
{
    return ::fwrite(ptr, size, nmemb, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::_getc(void* stream)
{
    return ::getc((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::getw(void* stream)
{
    return ::getw((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::_putc(int c, void* stream)
{
    return ::putc(c, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::putw(int w, void* stream)
{
    return ::putw(w, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::rewind(void* stream)
{
    return ::rewind((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setbuf(void* stream, char* buf)
{
    return ::setbuf((FILE*)stream, buf);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setbuffer(void* stream, char* buf, size_t size)
{
    return ::setbuffer((FILE*)stream, buf, size);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setlinebuf(void* stream)
{
    return ::setlinebuf((FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::setvbuf(void* stream, char* buf, int mode, size_t size)
{
    return ::setvbuf((FILE*)stream, buf, mode, size);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ungetc(int c, void* stream)
{
    return ::ungetc(c, (FILE*)stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::vfprintf(void* stream, const char* format, va_list ap)
{
    return ::vfprintf((FILE*)stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::vfscanf(void* stream, const char* format, va_list ap)
{
    return ::vfscanf((FILE*)stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------