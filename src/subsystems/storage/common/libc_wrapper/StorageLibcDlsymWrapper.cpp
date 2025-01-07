///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// This implementation provides the original libc implementation of the intercepted libc calls, in case the intercepted
// libc calls need to fallback to their original counterpart. It loads the "next" libc call in the load order.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageLibcApiWrapper.h"
#include <dlfcn.h>
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::open(const char* pathname, int flags, mode_t mode=0777)
{
    static Original_open m_originalOpen = (Original_open)dlsym(RTLD_NEXT, "open");
    return m_originalOpen(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::open64(const char* pathname, int flags, mode_t mode=0777)
{
    static Original_open64 m_originalOpen64 = (Original_open64)dlsym(RTLD_NEXT, "open64");
    return m_originalOpen64(pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::openat(int dirfd, const char* pathname, int flags, mode_t mode)
{
    static Original_openat m_originalOpenAt = (Original_openat)dlsym(RTLD_NEXT, "openat");
    return m_originalOpenAt(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::openat64(int dirfd, const char* pathname, int flags, mode_t mode)
{
    static Original_openat64 m_originalOpenAt64 = (Original_openat64)dlsym(RTLD_NEXT, "openat64");
    return m_originalOpenAt64(dirfd, pathname, flags, mode);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::fopen(const char* pathname, const char* mode)
{
    static Original_fopen m_originalFopen = (Original_fopen)dlsym(RTLD_NEXT, "fopen");
    return m_originalFopen(pathname,  mode);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::fopen64(const char* pathname, const char* mode)
{
    static Original_fopen64 m_originalFopen64 = (Original_fopen64)dlsym(RTLD_NEXT, "fopen64");
    return m_originalFopen64(pathname,  mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fileno(void* stream)
{
    static Original_fileno m_originalFileno = (Original_fileno)dlsym(RTLD_NEXT, "fileno");
    return m_originalFileno(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fclose(void* stream)
{
    static Original_fclose m_originalFClose = (Original_fclose)dlsym(RTLD_NEXT, "fclose");
    return m_originalFClose(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::close(int fd)
{
    static Original_close m_originalClose = (Original_close)dlsym(RTLD_NEXT, "close");
    return m_originalClose(fd);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::read(int fd, void *buf, size_t nByte)
{
    static Original_read m_originalRead = (Original_read)dlsym(RTLD_NEXT, "read");
    return m_originalRead(fd, buf, nByte);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::pread(int fd, void *buf, size_t nByte, off64_t offset)
{
    static Original_pread m_originalPRead = (Original_pread)dlsym(RTLD_NEXT, "pread");
    return m_originalPRead(fd, buf, nByte, offset);
}
//---------------------------------------------------------------------------------------------------------------------
off64_t StorageLibcApiWrapper::lseek64(int fd, off64_t offset, int whence)
{
    static Original_lseek64 m_OriginalLseek64 = (Original_lseek64)dlsym(RTLD_NEXT, "lseek64");
    return m_OriginalLseek64(fd, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::opendir(const char* pathname)
{
    static Original_opendir m_OriginalOpendir = (Original_opendir)dlsym(RTLD_NEXT, "opendir");
    return m_OriginalOpendir(pathname);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::readdir(void* dirp)
{
    static Original_readdir m_OriginalReaddir = (Original_readdir)dlsym(RTLD_NEXT, "readdir");
    return m_OriginalReaddir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::readdir64(void* dirp)
{
    static Original_readdir64 m_OriginalReaddir64 = (Original_readdir64)dlsym(RTLD_NEXT, "readdir64");
    return m_OriginalReaddir64(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::closedir(void* dirp)
{
    static Original_closedir m_OriginalClosedir = (Original_closedir)dlsym(RTLD_NEXT, "closedir");
    return m_OriginalClosedir(dirp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::access(const char* pathname, int mode)
{
    static Original_access m_OriginalAccess = (Original_access)dlsym(RTLD_NEXT, "access");
    return m_OriginalAccess(pathname, mode);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::xstat(int __ver, const char* pathname, struct stat* statbuf)
{
    static Original_xstat m_OriginalXstat = (Original_xstat)dlsym(RTLD_NEXT, "__xstat");
    return m_OriginalXstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::xstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    static Original_xstat64 m_OriginalXstat64 = (Original_xstat64)dlsym(RTLD_NEXT, "__xstat64");
    return m_OriginalXstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::lxstat(int __ver, const char* pathname, struct stat* statbuf)
{
    static Original_lxstat m_OriginalLxstat = (Original_lxstat)dlsym(RTLD_NEXT, "__lxstat");
    return m_OriginalLxstat(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::lxstat64(int __ver, const char* pathname, struct stat64* statbuf)
{
    static Original_lxstat64 m_OriginalLxstat64 = (Original_lxstat64)dlsym(RTLD_NEXT, "__lxstat64");
    return m_OriginalLxstat64(__ver, pathname, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fstat(int fd, struct stat* statbuf)
{
    static Original_fstat m_OriginalFstat = (Original_fstat)dlsym(RTLD_NEXT, "fstat");
    return m_OriginalFstat(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fstat64(int fd, struct stat64* statbuf)
{
    static Original_fstat64 m_OriginalFstat64 = (Original_fstat64)dlsym(RTLD_NEXT, "fstat64");
    return m_OriginalFstat64(fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fxstat(int __ver, int fd, struct stat* statbuf)
{
    static Original_fxstat m_OriginalFxstat = (Original_fxstat)dlsym(RTLD_NEXT, "__fxstat");
    return m_OriginalFxstat(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fxstat64(int __ver, int fd, struct stat64* statbuf)
{
    static Original_fxstat64 m_OriginalFxstat64 = (Original_fxstat64)dlsym(RTLD_NEXT, "__fxstat64");
    return m_OriginalFxstat64(__ver, fd, statbuf);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ioctl(int fd, unsigned long request, void* argp)
{
    static Original_ioctl m_OriginalIoctl = (Original_ioctl)dlsym(RTLD_NEXT, "ioctl");
    return m_OriginalIoctl(fd, request, argp);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::isatty(int fd)
{
    static Original_isatty m_OriginalIsatty = (Original_isatty)dlsym(RTLD_NEXT, "isatty");
    return m_OriginalIsatty(fd);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::flock(int fd, int operation)
{
    static Original_flock m_OriginalFlock = (Original_flock)dlsym(RTLD_NEXT, "flock");
    return m_OriginalFlock(fd, operation);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StorageLibcApiWrapper::listxattr(const char* path, char* list, size_t size)
{
    static Original_listxattr m_OriginalListxattr = (Original_listxattr)dlsym(RTLD_NEXT, "listxattr");
    return m_OriginalListxattr(path, list, size);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::clearerr(void* stream)
{
    static Original_clearerr m_Originalclearerr = (Original_clearerr)dlsym(RTLD_NEXT, "clearerr");
    return m_Originalclearerr(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::feof(void* stream)
{
    static Original_feof m_Originalfeof = (Original_feof)dlsym(RTLD_NEXT, "feof");
    return m_Originalfeof(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ferror(void* stream)
{
    static Original_ferror m_Originalferror = (Original_ferror)dlsym(RTLD_NEXT, "ferror");
    return m_Originalferror(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fflush(void* stream)
{
    static Original_fflush m_Originalfflush = (Original_fflush)dlsym(RTLD_NEXT, "fflush");
    return m_Originalfflush(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetc(void* stream)
{
    static Original_fgetc m_Originalfgetc = (Original_fgetc)dlsym(RTLD_NEXT, "fgetc");
    return m_Originalfgetc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetpos(void* stream, void* pos)
{
    static Original_fgetpos m_Originalfgetpos = (Original_fgetpos)dlsym(RTLD_NEXT, "fgetpos");
    return m_Originalfgetpos(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fgetpos64(void* stream, void* pos)
{
    static Original_fgetpos64 m_Originalfgetpos64 = (Original_fgetpos64)dlsym(RTLD_NEXT, "fgetpos64");
    return m_Originalfgetpos64(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
char* StorageLibcApiWrapper::fgets(char* s, int size, void* stream)
{
    static Original_fgets m_Originalfgets = (Original_fgets)dlsym(RTLD_NEXT, "fgets");
    return m_Originalfgets(s, size, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fprintf(void* stream, const char* format, ...)
{
    static Original_vfprintf m_Originalvfprintf = (Original_vfprintf)dlsym(RTLD_NEXT, "vfprintf");
    va_list args;
    va_start(args, format);
    int ret = m_Originalvfprintf(stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fpurge(void* stream)
{
    static Original_fpurge m_Originalfpurge = (Original_fpurge)dlsym(RTLD_NEXT, "fpurge");
    return m_Originalfpurge(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fputc(int c, void* stream)
{
    static Original_fputc m_Originalfputc = (Original_fputc)dlsym(RTLD_NEXT, "fputc");
    return m_Originalfputc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fputs(const char* s, void* stream)
{
    static Original_fputs m_Originalfputs = (Original_fputs)dlsym(RTLD_NEXT, "fputs");
    return m_Originalfputs(s, stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageLibcApiWrapper::fread(void* ptr, size_t size, size_t nmemb, void* stream)
{
    static Original_fread m_Originalfread = (Original_fread)dlsym(RTLD_NEXT, "fread");
    return m_Originalfread(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::freopen(const char* pathname, const char* mode, void* stream)
{
    static Original_freopen m_Originalfreopen = (Original_freopen)dlsym(RTLD_NEXT, "freopen");
    return m_Originalfreopen(pathname, mode, stream);
}
//---------------------------------------------------------------------------------------------------------------------
void* StorageLibcApiWrapper::freopen64(const char* pathname, const char* mode, void* stream)
{
    static Original_freopen64 m_Originalfreopen64 = (Original_freopen64)dlsym(RTLD_NEXT, "freopen64");
    return m_Originalfreopen64(pathname, mode, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fscanf(void* stream, const char* format, ...)
{
    static Original_vfscanf m_Originalvfscanf = (Original_vfscanf)dlsym(RTLD_NEXT, "vfscanf");
    va_list args;
    va_start(args, format);
    int ret = m_Originalvfscanf(stream, format, args);
    va_end(args);
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fseek(void* stream, long offset, int whence)
{
    static Original_fseek m_Originalfseek = (Original_fseek)dlsym(RTLD_NEXT, "fseek");
    return m_Originalfseek(stream, offset, whence);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fsetpos(void* stream, const void* pos)
{
    static Original_fsetpos m_Originalfsetpos = (Original_fsetpos)dlsym(RTLD_NEXT, "fsetpos");
    return m_Originalfsetpos(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::fsetpos64(void* stream, const void* pos)
{
    static Original_fsetpos64 m_Originalfsetpos64 = (Original_fsetpos64)dlsym(RTLD_NEXT, "fsetpos64");
    return m_Originalfsetpos64(stream, pos);
}
//---------------------------------------------------------------------------------------------------------------------
long StorageLibcApiWrapper::ftell(void* stream)
{
    static Original_ftell m_Originalftell = (Original_ftell)dlsym(RTLD_NEXT, "ftell");
    return m_Originalftell(stream);
}
//---------------------------------------------------------------------------------------------------------------------
size_t StorageLibcApiWrapper::fwrite(const void* ptr, size_t size, size_t nmemb, void* stream)
{
    static Original_fwrite m_Originalfwrite = (Original_fwrite)dlsym(RTLD_NEXT, "fwrite");
    return m_Originalfwrite(ptr, size, nmemb, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::_getc(void* stream)
{
    // In glibc _IO_getc is a weak symbol alias for getc
    static Original_getc m_Originalgetc = (Original_getc)dlsym(RTLD_NEXT, "getc");
    return m_Originalgetc(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::getw(void* stream)
{
    static Original_getw m_Originalgetw = (Original_getw)dlsym(RTLD_NEXT, "getw");
    return m_Originalgetw(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::_putc(int c, void* stream)
{
    // In glibc _IO_putc is a weak symbol alias for putc
    static Original_putc m_Originalputc = (Original_putc)dlsym(RTLD_NEXT, "putc");
    return m_Originalputc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::putw(int w, void* stream)
{
    static Original_putw m_Originalputw = (Original_putw)dlsym(RTLD_NEXT, "putw");
    return m_Originalputw(w, stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::rewind(void* stream)
{
    static Original_rewind m_Originalrewind = (Original_rewind)dlsym(RTLD_NEXT, "rewind");
    return m_Originalrewind(stream);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setbuf(void* stream, char* buf)
{
    static Original_setbuf m_Originalsetbuf = (Original_setbuf)dlsym(RTLD_NEXT, "setbuf");
    return m_Originalsetbuf(stream, buf);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setbuffer(void* stream, char* buf, size_t size)
{
    static Original_setbuffer m_Originalsetbuffer = (Original_setbuffer)dlsym(RTLD_NEXT, "setbuffer");
    return m_Originalsetbuffer(stream, buf, size);
}
//---------------------------------------------------------------------------------------------------------------------
void StorageLibcApiWrapper::setlinebuf(void* stream)
{
    static Original_setlinebuf m_Originalsetlinebuf = (Original_setlinebuf)dlsym(RTLD_NEXT, "setlinebuf");
    return m_Originalsetlinebuf(stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::setvbuf(void* stream, char* buf, int mode, size_t size)
{
    static Original_setvbuf m_Originalsetvbuf = (Original_setvbuf)dlsym(RTLD_NEXT, "setvbuf");
    return m_Originalsetvbuf(stream, buf, mode, size);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::ungetc(int c, void* stream)
{
    static Original_ungetc m_Originalungetc = (Original_ungetc)dlsym(RTLD_NEXT, "ungetc");
    return m_Originalungetc(c, stream);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::vfprintf(void* stream, const char* format, va_list ap)
{
    static Original_vfprintf m_Originalvfprintf = (Original_vfprintf)dlsym(RTLD_NEXT, "vfprintf");
    return m_Originalvfprintf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageLibcApiWrapper::vfscanf(void* stream, const char* format, va_list ap)
{
    static Original_vfscanf m_Originalvfscanf = (Original_vfscanf)dlsym(RTLD_NEXT, "vfscanf");
    return m_Originalvfscanf(stream, format, ap);
}
//---------------------------------------------------------------------------------------------------------------------