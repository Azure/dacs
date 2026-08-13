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
      /*[in]*/ const std::string& remotePath,
      /*[in]*/ const std::string& version) override;

  private:
    void GetConfig(const std::string& mountName);

  private:
    AdlsHostContext                         m_HostContext;
    std::unique_ptr<AdlsHttpClient>         m_spHttpClient;
};
//---------------------------------------------------------------------------------------------------------------------
