//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <map>
#include <mutex>
#include <vector>
#include "AdlsApiBase.h"
#include "AdlsHostContext.h"
#include "AdlsAuthToken.h"
#include "AdlsHttpClient.h"
#include "IRemoteClient.h"
#include "AdlsClientException.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsClient : public IRemoteClient
{
  public:
    AdlsClient(const std::string& mountName);

    ssize_t ReadChunkSync(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ void *buf,
      /*[in] */ size_t size,
      /*[in] */ off_t offset) override;

    std::string ReadDirectory(
      /*[in] */ const std::string& remotePath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries) override;

    int GetProperties(
      /*[in] */ const std::string& remotePath,
      /*[out]*/ FileAttributes& attr) override;

    void PrepareBlocksForUpload(
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ size_t numBlocks) override;

    int StageBlocksForUpload(
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ const void* buf,
      /*[in]*/ size_t length,
      /*[in]*/ off64_t blockId) override;
    
    int CommitBlocksForUpload(
      /*[in]*/ const std::string& remotePath) override;

  private:
    void GetConfig(const std::string& mountName);

  private:
    AdlsHostContext                         m_HostContext;
    std::unique_ptr<AdlsHttpClient>         m_spHttpClient;
};
//---------------------------------------------------------------------------------------------------------------------
