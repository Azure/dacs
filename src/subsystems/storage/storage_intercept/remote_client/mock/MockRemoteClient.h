//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IRemoteClient.h"
//---------------------------------------------------------------------------------------------------------------------
class MockRemoteClient : public IRemoteClient
{
  public:
    MockRemoteClient();
  
  public:
    ssize_t ReadChunkSync(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ void *buf,
      /*[in] */ size_t length,
      /*[in] */ off_t offset) override;

    std::string ReadDirectory(
      /*[in] */ const std::string& remotePath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries) override;

    int GetProperties(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ FileAttributes& attr) override;

  private:
    const std::string m_strFsRoot;
};
//---------------------------------------------------------------------------------------------------------------------
