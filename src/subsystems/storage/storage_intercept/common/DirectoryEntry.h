///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
//---------------------------------------------------------------------------------------------------------------------
const unsigned short D_NAME_LEN = 1024;
const ino64_t        D_INO_VAL = 2; // TODO: remove
//---------------------------------------------------------------------------------------------------------------------
// Struct similar to dirent64 for storing file entries
struct DirectoryEntry
{
  ino64_t             d_ino;       /* Inode number */
  off64_t             d_off;       /* Offset indicating position in directory stream */
  unsigned short      d_reclen;    /* Length of this record */
  unsigned char       d_type;      /* Type of file */
  char                d_name[D_NAME_LEN]; /* Null-terminated filename */
  off64_t             d_size;      /* Length of this file, 0 if its a directory */
  time_t              d_atime;     /* Last accessed time */
  time_t              d_mtime;     /* Last modified time */
};
//---------------------------------------------------------------------------------------------------------------------
