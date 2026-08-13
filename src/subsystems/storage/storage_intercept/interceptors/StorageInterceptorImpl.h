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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "../common/FileStat.h"
//---------------------------------------------------------------------------------------------------------------------
class StorageInterceptorImpl
{
    public:
        static int open(const char* pathname, int flags, mode_t mode);
        static int open64(const char* pathname, int flags, mode_t mode);

        static int openat(int dirfd, const char *pathname, int flags, mode_t mode);
        static int openat64(int dirfd, const char *pathname, int flags, mode_t mode);

        static void* fopen(const char* pathname, const char* mode);
        static void* fopen64(const char* pathname, const char* mode);
        static int fileno(void* stream);
        static int fclose(void* stream);

        static ssize_t read(int fd, void* buf, size_t size);
        static ssize_t pread(int fd, void* buf, size_t size, off64_t offset);
        static off64_t lseek(int fd, off64_t offset, int whence);
        static off64_t lseek64(int fd, off64_t offset, int whence);

        static ssize_t pwrite(int fd, const void* buf, size_t size, off64_t offset);

        static int close(int fd);

        static void* opendir(const char* pathname);
        static void* readdir(void* dirp);
        static void* readdir64(void* dirp);
        static int closedir(void* dirp);

        static int access(const char *pathname, int mode);
        static int stat(const char* pathname, struct stat* statbuf);
        static int stat64(const char* pathname, struct stat64* statbuf);
        static int dacs_stat(const char* pathname, struct FileStat* statbuf);
        static int xstat(int __ver, const char* pathname, struct stat* statbuf);
        static int xstat64(int __ver, const char* pathname, struct stat64* statbuf);
        static int fstat(int fd, struct stat* statbuf);
        static int fstat64(int fd, struct stat64* statbuf);
        static int fxstat(int __ver, int fd, struct stat* statbuf);
        static int fxstat64(int __ver, int fd, struct stat64* statbuf);
        static int lxstat(int __ver, const char* pathname, struct stat* statbuf);
        static int lxstat64(int __ver, const char* pathname, struct stat64* statbuf);
        static ssize_t listxattr(const char* path, char* list, size_t size);

        static int ioctl(int fd, unsigned long request, void* argp);
        static int isatty(int fd);
        static int flock(int fd, int operation);

        static void clearerr(void* stream);
        static int feof(void* stream);
        static int ferror(void* stream);
        static int fflush(void* stream);
        static int fgetc(void* stream);
        static int fgetpos(void* stream, void* pos);
        static int fgetpos64(void* stream, void* pos);
        static char* fgets(char* s, int size, void* stream);
        static int fpurge(void* stream);
        static int fputc(int c, void* stream);
        static int fputs(const char* s, void* stream);
        static size_t fread(void* ptr, size_t size, size_t nmemb, void* stream);
        static void* freopen(const char* pathname, const char* mode, void* stream);
        static void* freopen64(const char* pathname, const char* mode, void* stream);
        static int fseek(void* stream, long offset, int whence);
        static int fsetpos(void* stream, const void* pos);
        static int fsetpos64(void* stream, const void* pos);
        static long ftell(void* stream);
        static size_t fwrite(const void* ptr, size_t size, size_t nmemb, void* stream);
        static int getc(void* stream);
        static int getw(void* stream);
        static int putc(int c, void* stream);
        static int putw(int w, void* stream);
        static void rewind(void* stream);
        static void setbuf(void* stream, char* buf);
        static void setbuffer(void* stream, char* buf, size_t size);
        static void setlinebuf(void* stream);
        static int setvbuf(void* stream, char* buf, int mode, size_t size);
        static int ungetc(int c, void* stream);
        static int vfprintf(void* stream, const char* format, va_list ap);
        static int vfscanf(void* stream, const char* format, va_list ap);

        static int SI_open(const char* pathname, int flags, int mode, char** errorString);
        static ssize_t SI_pread(int fd, void* buf, size_t count, off64_t offset, char** errorString);
        static int SI_close(int fd, char** errorString);
        static int SI_fstat(int fd, struct stat* statbuf, char** errorString);
};
//---------------------------------------------------------------------------------------------------------------------