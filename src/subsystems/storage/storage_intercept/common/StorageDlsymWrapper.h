///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// The StorageDlsymWrapper provides the original libc implementation of the intercepted libc calls, in case the 
// intercepted libc calls need to fallback to their original counterpart. It loads the "next" libc call in the load
// order.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdarg.h>
// TODO: simplify this class using macros
//---------------------------------------------------------------------------------------------------------------------
class StorageDlsymWrapper
{
    public:
        static int open(/*[in]*/ const char* pathname,
                        /*[in]*/ int flags,
                        /*[in]*/ mode_t mode);

        static int open64(/*[in]*/ const char* pathname,
                          /*[in]*/ int flags,
                          /*[in]*/ mode_t mode);

        static int openat(/*[in]*/ int dirfd,
                        /*[in]*/ const char* pathname,
                        /*[in]*/ int flags,
                        /*[in]*/ mode_t mode);

        static int openat64(/*[in]*/ int dirfd,
                          /*[in]*/ const char* pathname,
                          /*[in]*/ int flags,
                          /*[in]*/ mode_t mode);

        static off64_t lseek64(/*[in]*/ int fd,
                             /*[in]*/ off64_t offset,
                             /*[in]*/ int whence);

        static int close(/*[in]*/ int fd);

        static int access(/*[in]*/ const char* pathname,
                         /*[in]*/ int mode);

        static int xstat(/*[in]*/ int __ver,
                         /*[in]*/ const char* pathname,
                         /*[in]*/ struct stat* statbuf);

        static int xstat64(/*[in]*/ int __ver,
                         /*[in]*/ const char* pathname,
                         /*[in]*/ struct stat64* statbuf);

        static int lxstat(/*[in]*/ int __ver,
                          /*[in]*/ const char* pathname,
                          /*[in]*/ struct stat* statbuf);

        static int lxstat64(/*[in]*/ int __ver,
                          /*[in]*/ const char* pathname,
                          /*[in]*/ struct stat64* statbuf);

        static int fstat(/*[in]*/ int fd,
                         /*[in]*/ struct stat* statbuf);

        static int fxstat(/*[in]*/ int __ver,
                          /*[in]*/ int fd,
                          /*[in]*/ struct stat* statbuf);
        
        static int fxstat64(/*[in]*/ int __ver,
                            /*[in]*/ int fd,
                            /*[in]*/ struct stat64* statbuf);

        static ssize_t read(/*[in]*/ int fd,
                            /*[out]*/ void *buf,
                            /*[in]*/ size_t nByte);

        static ssize_t pread(/*[in]*/ int fd,
                             /*[out]*/ void *buf,
                             /*[in]*/ size_t nByte,
                             /*[in]*/ off64_t offset);

        static void* opendir(/*[in]*/ const char* pathname);

        static void* readdir(/*[in]*/ void* dirp);

        static void* readdir64(/*[in]*/ void* dirp);

        static int closedir(/*[in]*/ void* dirp);

        static int ioctl(/*[in]*/ int fd, /*[in]*/ unsigned long request, /*[out]*/ void* argp);
        static int isatty(/*[in]*/ int fd);
        static int flock(/*[in]*/ int fd, /*[in]*/ int operation);
        static ssize_t listxattr(/*[in]*/ const char* path, /*[out]*/ char* list, /*[in]*/ size_t size);

        // ******** File stream APIs -- Start ****************/
        static void* fopen(/*[in]*/ const char* pathname, /*[in]*/ const char* mode);
        static void* fopen64(/*[in]*/ const char* pathname, /*[in]*/ const char* mode);
        static int fileno(/*[in]*/ void* stream);
        static int fclose(/*[in]*/ void* stream);
        static void clearerr(/*[in]*/ void* stream);
        static int feof(/*[in]*/ void* stream);
        static int ferror(/*[in]*/ void* stream);
        static int fflush(/*[in]*/ void* stream);
        static int fgetc(/*[in]*/ void* stream);
        static int fgetpos(/*[in]*/ void* stream, /*[out]*/ void* pos);
        static int fgetpos64(/*[in]*/ void* stream, /*[out]*/ void* pos);
        static char* fgets(/*[out]*/ char* s, int size, /*[in]*/ void* stream);
        static int fprintf(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[in]*/ ...);
        static int fpurge(/*[in]*/ void* stream);
        static int fputc(/*[in]*/ int c, /*[in]*/ void* stream);
        static int fputs(/*[in]*/ const char* s, /*[in]*/ void* stream);
        static size_t fread(/*[out]*/ void* ptr, /*[in]*/ size_t size, /*[in]*/ size_t nmemb, /*[in]*/ void* stream);
        static void* freopen(/*[in]*/ const char* pathname, /*[in]*/ const char* mode, /*[in]*/ void* stream);
        static void* freopen64(/*[in]*/ const char* pathname, /*[in]*/ const char* mode, /*[in]*/ void* stream);
        static int fscanf(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[out]*/ ...);
        static int fseek(/*[in]*/ void* stream, /*[in]*/ long offset, /*[in]*/ int whence);
        static int fsetpos(/*[in]*/ void* stream, /*[in]*/ const void* pos);
        static int fsetpos64(/*[in]*/ void* stream, /*[in]*/ const void* pos);
        static long ftell(/*[in]*/ void* stream);
        static size_t fwrite(/*[in]*/ const void* ptr, /*[in]*/ size_t size, /*[in]*/ size_t nmemb,
            /*[in]*/ void* stream);
        static int getc(/*[in]*/ void* stream);
        static int getw(/*[in]*/ void* stream);
        static int putc(/*[in]*/ int c, /*[in]*/ void* stream);
        static int putw(/*[in]*/ int w, /*[in]*/ void* stream);
        static void rewind(/*[in]*/ void* stream);
        static void setbuf(/*[in]*/ void* stream, /*[in]*/ char* buf);
        static void setbuffer(/*[in]*/ void* stream, /*[in]*/ char* buf, /*[in]*/ size_t size);
        static void setlinebuf(/*[in]*/ void* stream);
        static int setvbuf(/*[in]*/ void* stream, /*[in]*/ char* buf, /*[in]*/ int mode, /*[in]*/ size_t size);
        static int ungetc(/*[in]*/ int c, /*[in]*/ void* stream);
        static int vfprintf(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[in]*/ va_list ap);
        static int vfscanf(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[out]*/ va_list ap);
        // ******** File stream APIs -- End ****************/

        typedef int (*Original_open) (/*[in]*/ const char* pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode);
        typedef int (*Original_open64) (/*[in]*/ const char* pathname, /*[in]*/ int flags, /*[in]*/ mode_t mode);
        typedef int (*Original_openat) (/*[in]*/ int dirfd, /*[in]*/ const char* pathname, /*[in]*/ int flags,
            /*[in]*/ mode_t mode);
        typedef int (*Original_openat64) (/*[in]*/ int dirfd, /*[in]*/ const char* pathname, /*[in]*/ int flags,
            /*[in]*/ mode_t mode);
        typedef int (*Original_close) (/*[in]*/ int fd);
        typedef ssize_t (*Original_read) (/*[in]*/ int fd, /*[out]*/ void *buf, /*[in]*/ size_t nByte);
        typedef ssize_t (*Original_pread) (/*[in]*/ int fd, /*[out]*/ void *buf, /*[in]*/ size_t nByte,
            /*[in]*/ off64_t offset);
        typedef off64_t (*Original_lseek64) (/*[in]*/ int fd, /*[in]*/ off64_t offset, /*[in]*/ int whence);
        typedef void* (*Original_opendir) (/*[in]*/ const char* pathname);
        typedef void* (*Original_readdir)(/*[in]*/ void* dirp);
        typedef void* (*Original_readdir64)(/*[in]*/ void* dirp);
        typedef int (*Original_closedir)(/*[in]*/ void* dirp);
        typedef int (*Original_access)(/*[in]*/ const char* pathname, /*[in]*/ int mode);
        typedef int (*Original_xstat)(/*[in]*/ int __ver, /*[in]*/ const char* pathname,
            /*[out]*/ struct stat* statbuf);
        typedef int (*Original_xstat64)(/*[in]*/ int __ver, /*[in]*/ const char* pathname,
            /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_fstat)(/*[in]*/ int fd, /*[out]*/ struct stat* statbuf);
        typedef int (*Original_fxstat)(/*[in]*/ int __ver, /*[in]*/ int fd, /*[out]*/ struct stat* statbuf);
        typedef int (*Original_fxstat64)(/*[in]*/ int __ver, /*[in]*/ int fd, /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_lxstat)(/*[in]*/ int __ver, /*[in]*/ const char* pathname,
            /*[out]*/ struct stat* statbuf);
        typedef int (*Original_lxstat64)(/*[in]*/ int __ver, /*[in]*/ const char* pathname,
            /*[out]*/ struct stat64* statbuf);
        typedef int (*Original_ioctl)(/*[in]*/ int fd, /*[in]*/ unsigned long request, /*[out]*/ void* argp);
        typedef int (*Original_isatty)(/*[in]*/ int fd);
        typedef int (*Original_flock)(/*[in]*/ int fd, /*[in]*/ int operation);
        typedef ssize_t (*Original_listxattr)(/*[in]*/ const char* path, /*[out]*/ char* list, /*[in]*/ size_t size);

        // ******** File stream API definitions -- Start ****************/
        typedef void* (*Original_fopen) (/*[in]*/ const char* pathname, /*[in]*/ const char* mode);
        typedef void* (*Original_fopen64) (/*[in]*/ const char* pathname, /*[in]*/ const char* mode);
        typedef int (*Original_fileno) (/*[in]*/ void* stream);
        typedef int (*Original_fclose) (/*[in]*/ void* stream);
        typedef void (*Original_clearerr) (/*[in]*/ void* stream);
        typedef int (*Original_feof)(/*[in]*/ void* stream);
        typedef int (*Original_ferror)(/*[in]*/ void* stream);
        typedef int (*Original_fflush)(/*[in]*/ void* stream);
        typedef int (*Original_fgetc)(/*[in]*/ void* stream);
        typedef int (*Original_fgetpos)(/*[in]*/ void* stream, /*[out]*/ void* pos);
        typedef int (*Original_fgetpos64)(/*[in]*/ void* stream, /*[out]*/ void* pos);
        typedef char* (*Original_fgets)(/*[out]*/ char* s, int size, /*[in]*/ void* stream);
        typedef int (*Original_fprintf)(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[in]*/ ...);
        typedef int (*Original_fpurge)(/*[in]*/ void* stream);
        typedef int (*Original_fputc)(/*[in]*/ int c, /*[in]*/ void* stream);
        typedef int (*Original_fputs)(/*[in]*/ const char* s, /*[in]*/ void* stream);
        typedef size_t (*Original_fread)(/*[out]*/ void* ptr, /*[in]*/ size_t size, /*[in]*/ size_t nmemb,
            /*[in]*/ void* stream);
        typedef void* (*Original_freopen)(/*[in]*/ const char* pathname, /*[in]*/ const char* mode,
            /*[in]*/ void* stream);
        typedef void* (*Original_freopen64)(/*[in]*/ const char* pathname, /*[in]*/ const char* mode,
            /*[in]*/ void* stream);
        typedef int (*Original_fscanf)(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[out]*/ ...);
        typedef int (*Original_fseek)(/*[in]*/ void* stream, /*[in]*/ long offset, /*[in]*/ int whence);
        typedef int (*Original_fsetpos)(/*[in]*/ void* stream, /*[in]*/ const void* pos);
        typedef int (*Original_fsetpos64)(/*[in]*/ void* stream, /*[in]*/ const void* pos);
        typedef long (*Original_ftell)(/*[in]*/ void* stream);
        typedef size_t (*Original_fwrite)(/*[in]*/ const void* ptr, /*[in]*/ size_t size, /*[in]*/ size_t nmemb,
            /*[in]*/ void* stream);
        typedef int (*Original_getc)(/*[in]*/ void* stream);
        typedef int (*Original_getw)(/*[in]*/ void* stream);
        typedef int (*Original_putc)(/*[in]*/ int c, /*[in]*/ void* stream);
        typedef int (*Original_putw)(/*[in]*/ int w, /*[in]*/ void* stream);
        typedef void (*Original_rewind)(/*[in]*/ void* stream);
        typedef void (*Original_setbuf)(/*[in]*/ void* stream, /*[in]*/ char* buf);
        typedef void (*Original_setbuffer)(/*[in]*/ void* stream, /*[in]*/ char* buf, /*[in]*/ size_t size);
        typedef void (*Original_setlinebuf)(/*[in]*/ void* stream);
        typedef int (*Original_setvbuf)(/*[in]*/ void* stream, /*[in]*/ char* buf, /*[in]*/ int mode,
            /*[in]*/ size_t size);
        typedef int (*Original_ungetc)(/*[in]*/ int c, /*[in]*/ void* stream);
        typedef int (*Original_vfprintf)(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[in]*/ va_list ap);
        typedef int (*Original_vfscanf)(/*[in]*/ void* stream, /*[in]*/ const char* format, /*[out]*/ va_list ap);
        // ******** File stream API definitions -- End ****************/
};
//---------------------------------------------------------------------------------------------------------------------