///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open(const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::open(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int open64(const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::open64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::openat(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int openat64(int dirfd, const char *pathname, int flags, mode_t mode)
{
    return StorageInterceptorImpl::openat64(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* fopen(const char* pathname, const char* mode)
{
    return StorageInterceptorImpl::fopen(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* fopen64(const char* pathname, const char* mode)
{
    return StorageInterceptorImpl::fopen64(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fileno(void* stream)
{
    return StorageInterceptorImpl::fileno(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fclose(void* stream)
{
    return StorageInterceptorImpl::fclose(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t __read_chk(int fd, void* buf, size_t nByte, size_t buflen)
{
    // According to API spec this is same as read() except we should abort if buffer overflow is anticipated
    // Ref: http://refspecs.linux-foundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/libc---read-chk-1.html
    if (nByte > buflen)
    {
        throw "buffer overflow detected";
    }

    return StorageInterceptorImpl::read(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t read(int fd, void* buf, size_t nByte)
{
    return StorageInterceptorImpl::read(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" off64_t lseek64(int fd, off64_t offset, int whence)
{
    return StorageInterceptorImpl::lseek64(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t pread(int fd, void* buf, size_t nByte, off64_t offset)
{
    return StorageInterceptorImpl::pread(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int close(int fd)
{
   return StorageInterceptorImpl::close(fd);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* opendir(const char *pathname)
{
    return StorageInterceptorImpl::opendir(pathname);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir(void* dirp)
{
    return StorageInterceptorImpl::readdir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* readdir64(void* dirp)
{
    return StorageInterceptorImpl::readdir64(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int closedir(void* dirp)
{
    return StorageInterceptorImpl::closedir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int access(const char *pathname, int mode)
{
    return StorageInterceptorImpl::access(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return StorageInterceptorImpl::xstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return StorageInterceptorImpl::xstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fstat(int fd, struct stat *statbuf)
{
    return StorageInterceptorImpl::fstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat(int __ver, int fd, struct stat* statbuf)
{
    return StorageInterceptorImpl::fxstat(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    return StorageInterceptorImpl::fxstat64(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    return StorageInterceptorImpl::lxstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int __lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    return StorageInterceptorImpl::lxstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ioctl(int fd, unsigned long request, void* argp)
{
    return StorageInterceptorImpl::ioctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int isatty(int fd)
{
    return StorageInterceptorImpl::isatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int flock(int fd, int operation)
{
    return StorageInterceptorImpl::flock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" ssize_t listxattr(const char* path, char* list, size_t size)
{
    return StorageInterceptorImpl::listxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void clearerr(void* stream)
{
    return StorageInterceptorImpl::clearerr(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int feof(void* stream)
{
    return StorageInterceptorImpl::feof(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ferror(void* stream)
{
    return StorageInterceptorImpl::ferror(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fflush(void* stream)
{
    return StorageInterceptorImpl::fflush(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetc(void* stream)
{
    return StorageInterceptorImpl::fgetc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetpos(void* stream, void* pos)
{
    return StorageInterceptorImpl::fgetpos(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fgetpos64(void* stream, void* pos)
{
    return StorageInterceptorImpl::fgetpos64(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" char* fgets(char* s, int size, void* stream)
{
    return StorageInterceptorImpl::fgets(s, size, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fprintf(void* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = StorageInterceptorImpl::vfprintf(stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fpurge(void* stream)
{
    return StorageInterceptorImpl::fpurge(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fputc(int c, void* stream)
{
    return StorageInterceptorImpl::fputc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fputs(const char* s, void* stream)
{
    return StorageInterceptorImpl::fputs(s, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" size_t fread(void* ptr, size_t size, size_t nmemb, void* stream)
{
    return StorageInterceptorImpl::fread(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* freopen(const char* pathname, const char* mode, void* stream)
{
    return StorageInterceptorImpl::freopen(pathname, mode, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void* freopen64(const char* pathname, const char* mode, void* stream)
{
    return StorageInterceptorImpl::freopen64(pathname, mode, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fscanf(void* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = StorageInterceptorImpl::vfscanf(stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fseek(void* stream, long offset, int whence)
{
    return StorageInterceptorImpl::fseek(stream, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fsetpos(void* stream, const void* pos)
{
    return StorageInterceptorImpl::fsetpos(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int fsetpos64(void* stream, const void* pos)
{
    return StorageInterceptorImpl::fsetpos64(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" long ftell(void* stream)
{
    return StorageInterceptorImpl::ftell(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" size_t fwrite(const void* ptr, size_t size, size_t nmemb, void* stream)
{
    return StorageInterceptorImpl::fwrite(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int getc(void* stream)
{
    return StorageInterceptorImpl::getc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int getw(void* stream)
{
    return StorageInterceptorImpl::getw(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int putc(int c, void* stream)
{
    return StorageInterceptorImpl::putc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int putw(int w, void* stream)
{
    return StorageInterceptorImpl::putw(w, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void rewind(void* stream)
{
    return StorageInterceptorImpl::rewind(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setbuf(void* stream, char* buf)
{
    return StorageInterceptorImpl::setbuf(stream, buf);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setbuffer(void* stream, char* buf, size_t size)
{
    return StorageInterceptorImpl::setbuffer(stream,buf, size);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" void setlinebuf(void* stream)
{
    return StorageInterceptorImpl::setlinebuf(stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int setvbuf(void* stream, char* buf, int mode, size_t size)
{
    return StorageInterceptorImpl::setvbuf(stream, buf, mode, size);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int ungetc(int c, void* stream)
{
    return StorageInterceptorImpl::ungetc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int vfprintf(void* stream, const char* format, va_list ap)
{
    return StorageInterceptorImpl::vfprintf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" int vfscanf(void* stream, const char* format, va_list ap)
{
    return StorageInterceptorImpl::vfscanf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------