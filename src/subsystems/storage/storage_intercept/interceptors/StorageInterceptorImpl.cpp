///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptorImpl.h"
#include "StorageLibcApiWrapper.h"
#include "ContentHandler.h"
#include "RemoteClientException.h"
#include <cstdlib>
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
off64_t StorageInterceptorImpl::lseek64(int fd, off64_t offset, int whence)
{
    return ContentHandler::GetInstance().HandleLseek(fd, offset, whence);
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
    catch(const RemoteClientException& e)
    {
        *errorString = ::strdup(e.what());
        errno = EIO;
        return -1;
    }

    return -1;   
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
        int ret = ContentHandler::GetInstance().HandlePRead(fd, buf, count, offset);
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
    catch(const RemoteClientException& e)
    {
        *errorString = ::strdup(e.what());
        errno = EIO;
        return -1;
    }

    return -1;   
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
    catch(const RemoteClientException& e)
    {
        *errorString = ::strdup(e.what());
        errno = EIO;
        return -1;
    }

    return -1;   
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
    catch(const RemoteClientException& e)
    {
        *errorString = ::strdup(e.what());
        errno = EIO;
        return -1;
    }

    return -1;   
}
//---------------------------------------------------------------------------------------------------------------------