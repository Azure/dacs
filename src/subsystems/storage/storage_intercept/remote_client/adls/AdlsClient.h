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
    AdlsClient();

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

  private:
    void GetConfig();

  private:
    AdlsHostContext                         m_HostContext;
    std::unique_ptr<AdlsHttpClient>         m_spHttpClient;
};
//---------------------------------------------------------------------------------------------------------------------
