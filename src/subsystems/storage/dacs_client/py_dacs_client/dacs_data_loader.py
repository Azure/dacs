# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Data Loader
# --------------------------------------------------------------------------------------------------------------------
from py_dacs_client.dacs_utils import logger
import os
from ctypes import (
    CDLL,
    POINTER,
    Structure,
    c_char,
    c_char_p,
    c_int,
    c_int64,
    c_long,
    c_size_t,
    c_ssize_t,
    c_ubyte,
    c_uint32,
    c_ulong,
    c_ushort,
    c_void_p,
)


# Architecture-agnostic file stat struct matching FileStat in native code.
# Uses fixed-width types so the layout is identical on all architectures.
# Only includes fields actually populated by the storage intercept layer.
class Stat64(Structure):
    _fields_ = [
        ("st_mode", c_uint32),
        ("st_size", c_int64),
        ("st_blksize", c_int64),
        ("st_atime", c_int64),
        ("st_mtime", c_int64),
        ("st_ctime", c_int64),
    ]


# Get the directory where the current python file exists
current_dir = os.path.dirname(os.path.abspath(__file__))

# Construct the full path to the shared library
# libStorageDirect.so is required - fail fast if not present
lib_path = os.path.join(current_dir, "libStorageDirect.so")
if not os.path.isfile(lib_path):
    error_msg = (
        f"FATAL: Required library 'libStorageDirect.so' not found!\n"
        f"  Expected path: {lib_path}\n"
        f"  Current directory: {current_dir}\n"
        f"  Directory contents: {os.listdir(current_dir) if os.path.isdir(current_dir) else 'N/A'}\n"
        f"\n"
    )
    logger.error(error_msg)
    raise FileNotFoundError(error_msg)

logger.info(f"Loading storage cache client library from path {lib_path}")

# Load the library with use_errno=True to properly capture errno values
storage_intercept_lib = CDLL(lib_path, use_errno=True)

# Defining the args and return types for the required functions in the shared library
storage_intercept_lib.open.argtypes = [c_char_p, c_int, c_int]
storage_intercept_lib.open.restype = c_int

storage_intercept_lib.pread.argtypes = [c_int, c_void_p, c_size_t, c_ulong]
storage_intercept_lib.pread.restype = c_ssize_t

storage_intercept_lib.pwrite.argtypes = [c_int, c_void_p, c_size_t, c_ulong]
storage_intercept_lib.pwrite.restype = c_ssize_t

storage_intercept_lib.close.argtypes = [c_int]
storage_intercept_lib.close.restype = c_int

storage_intercept_lib.dacs_stat.argtypes = [c_char_p, POINTER(Stat64)]
storage_intercept_lib.dacs_stat.restype = c_int

# struct dirent *readdir(DIR *dirp);

# The readdir() function returns a pointer to a dirent structure representing the next directory entry in the directory
# stream pointed to by dirp. It returns NULL on reaching the end of the directory stream or if an error occurred.

# In the glibc implementation, the dirent structure is defined as follows:

#     struct dirent {
#         ino_t          d_ino;       /* Inode number */
#         off_t          d_off;       /* Not an offset; see below */
#         unsigned short d_reclen;    /* Length of this record */
#         unsigned char  d_type;      /* Type of file; not supported
#                                         by all filesystem types */
#         char           d_name[256]; /* Null-terminated filename */
#     };
# DACS codes
# struct DirectoryEntry
# {
#   ino64_t             d_ino;       /* Inode number */
#   off64_t             d_off;       /* Offset indicating position in directory stream */
#   unsigned short      d_reclen;    /* Length of this record */
#   unsigned char       d_type;      /* Type of file */
#   char                d_name[D_NAME_LEN]; /* Null-terminated filename */
#   off64_t             d_size;      /* Length of this file, 0 if its a directory */
#   time_t              d_atime;     /* Last accessed time */
#   time_t              d_mtime;     /* Last modified time */
#   char                d_version[D_VERSION_LEN]; /* File version */
# };


# Define the structure for dirent
class DirectoryEntry(Structure):
    _fields_ = [
        ("d_ino", c_ulong),
        ("d_off", c_long),
        ("d_reclen", c_ushort),
        ("d_type", c_ubyte),
        ("d_name", c_char * 1024),
        ("d_size", c_long),
        ("d_atime", c_long),
        ("d_mtime", c_long),
        ("d_version", c_char * 40),  # Internal field, required for struct layout, not exposed to users
    ]


storage_intercept_lib.opendir.argtypes = [c_char_p]
storage_intercept_lib.opendir.restype = c_void_p

storage_intercept_lib.readdir.argtypes = [c_void_p]
storage_intercept_lib.readdir.restype = POINTER(DirectoryEntry)

storage_intercept_lib.closedir.argtypes = [c_void_p]
storage_intercept_lib.closedir.restype = c_int


# Python wrapper functions for the C functions in the shared library
class DACSCacheDataLoader:
    @staticmethod
    def open(pathname: str, flags: int, mode: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.open for pathname {pathname} with flags {flags} and mode {mode}")
        return storage_intercept_lib.open(pathname.encode("utf-8"), flags, mode)

    @staticmethod
    def pread(fd: int, buf: c_void_p, size: int, offset: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.pread for fd {fd} with size {size} and offset {offset}")
        return storage_intercept_lib.pread(fd, buf, size, offset)

    @staticmethod
    def pwrite(fd: int, buf: c_void_p, size: int, offset: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.pwrite for fd {fd} with size {size} and offset {offset}")
        return storage_intercept_lib.pwrite(fd, buf, size, offset)

    @staticmethod
    def close(fd: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.close for fd {fd}")
        return storage_intercept_lib.close(fd)

    @staticmethod
    def stat64(pathname: str, statbuf: POINTER(Stat64)) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.stat64 for path {pathname}")
        return storage_intercept_lib.dacs_stat(pathname.encode("utf-8"), statbuf)

    @staticmethod
    def opendir(pathname: str) -> c_void_p:
        logger.debug(f"Entering DACSCacheDataLoader.opendir for pathname {pathname}")
        return storage_intercept_lib.opendir(pathname.encode("utf-8"))

    @staticmethod
    def readdir(dirp: c_void_p) -> POINTER(DirectoryEntry):
        logger.debug(f"Entering DACSCacheDataLoader.readdir for dirp {dirp}")
        return storage_intercept_lib.readdir(dirp)

    @staticmethod
    def closedir(dirp: c_void_p) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.closedir for dirp {dirp}")
        return storage_intercept_lib.closedir(dirp)
