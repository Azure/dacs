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
//---------------------------------------------------------------------------------------------------------------------
const unsigned short D_NAME_LEN = 1024;
const ino64_t        D_INO_VAL = 2; // TODO: remove
const unsigned short D_VERSION_LEN = 40; // Holds dacsfileversion UUID (36 chars) or Azure ETag (~19 chars)
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
  char                d_version[D_VERSION_LEN]; /* File version */
};
//---------------------------------------------------------------------------------------------------------------------
