// Copyright (c) Microsoft Corporation.
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
#include <vector>
#include <string>
#include "FileAttributes.h"
#include "DirectoryEntry.h"
//---------------------------------------------------------------------------------------------------------------------
class IRemoteClient
{
  public:
    virtual ssize_t ReadChunkSync(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ void *buf,
      /*[in] */ size_t length,
      /*[in] */ off_t offset) = 0;

    virtual std::string ReadDirectory(
      /*[in] */ const std::string& remotePath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries) = 0;

    virtual int GetProperties(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ FileAttributes& attr) = 0;

    virtual void PrepareBlocksForUpload(
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ size_t numBlocks) = 0;

    virtual int StageBlocksForUpload(
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ const void* buf,
      /*[in]*/ size_t length,
      /*[in]*/ off64_t blockId) = 0;
    
    virtual int CommitBlocksForUpload(
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ const std::string& version) = 0;

    virtual ~IRemoteClient() {};
};
//---------------------------------------------------------------------------------------------------------------------
