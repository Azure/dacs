//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
      /*[in]*/ const std::string& remotePath) = 0;

    virtual ~IRemoteClient() {};
};
//---------------------------------------------------------------------------------------------------------------------
