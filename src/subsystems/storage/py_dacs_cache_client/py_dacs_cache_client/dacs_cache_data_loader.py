# --------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# DACS Cache Data Loader
# --------------------------------------------------------------------------------------------------------------------
from py_dacs_cache_client.dacs_cache_utils import logger
import os
from ctypes import (
    CDLL,
    POINTER,
    Structure,
    c_char,
    c_char_p,
    c_int,
    c_long,
    c_size_t,
    c_ssize_t,
    c_ubyte,
    c_uint,
    c_ulong,
    c_ushort,
    c_void_p,
)


class Stat64(Structure):
    _fields_ = [
        # Define the fields of the struct stat64 here
        ("st_dev", c_ulong),
        ("st_ino", c_ulong),
        ("st_mode", c_uint),
        ("st_nlink", c_ulong),
        ("st_uid", c_uint),
        ("st_gid", c_uint),
        ("st_rdev", c_ulong),
        ("st_size", c_long),
        ("st_blksize", c_long),
        ("st_blocks", c_long),
        ("st_atime", c_long),
        ("st_atimensec", c_long),
        ("st_mtime", c_long),
        ("st_mtimensec", c_long),
        ("st_ctime", c_long),
        ("st_ctimensec", c_long),
    ]


# Get the directory where the current python file exists
current_dir = os.path.dirname(os.path.abspath(__file__))

# Construct the full path to the shared library
lib_path = os.path.join(current_dir, "libStorageIntercept.so")
logger.debug(f"Loading shared library from path {lib_path}")

# Load the shared libStorageIntercept library
storage_intercept_lib = CDLL(lib_path)

# Defining the args and return types for the required functions in the shared library
storage_intercept_lib.open.argtypes = [c_char_p, c_int, c_int]
storage_intercept_lib.open.restype = c_int

storage_intercept_lib.pread.argtypes = [c_int, c_void_p, c_size_t, c_ulong]
storage_intercept_lib.pread.restype = c_ssize_t

storage_intercept_lib.close.argtypes = [c_int]
storage_intercept_lib.close.restype = c_int

storage_intercept_lib.stat64.argtypes = [c_char_p, POINTER(Stat64)]
storage_intercept_lib.stat64.restype = c_int

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
# };


# Define the structure for dirent
class DirectoryEntry(Structure):
    _fields_ = [
        ("d_ino", c_ulong),
        ("d_off", c_long),
        ("d_reclen", c_ushort),
        ("d_type", c_ubyte),
        ("d_name", c_char * 1025),  # Assuming max filename length is 1024 + null terminator
        ("d_size", c_long),
        ("d_atime", c_long),
        ("d_mtime", c_long),
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
        logger.debug(
            f"Entering DACSCacheDataLoader.open for pathname {pathname} with flags {flags} and mode {mode}"
        )
        return storage_intercept_lib.open(pathname.encode("utf-8"), flags, mode)

    @staticmethod
    def pread(fd: int, buf: c_void_p, size: int, offset: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.pread for fd {fd} with size {size} and offset {offset}")
        return storage_intercept_lib.pread(fd, buf, size, offset)

    @staticmethod
    def close(fd: int) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.close for fd {fd}")
        return storage_intercept_lib.close(fd)

    @staticmethod
    def stat64(pathname: str, statbuf: POINTER(Stat64)) -> int:
        logger.debug(f"Entering DACSCacheDataLoader.stat64 for path {pathname}")
        return storage_intercept_lib.stat64(pathname.encode("utf-8"), statbuf)

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
