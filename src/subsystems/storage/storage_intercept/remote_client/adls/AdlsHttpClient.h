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
#include "CurlHttpClient.h"
#include <jsoncpp/json/json.h>
#include "DirectoryEntry.h"
#include "FileAttributes.h"
#include <memory>
#include "AdlsHttpUrlBuilder.h"
#include "CurlClientException.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsHttpClient
{
  public:
    void GetAuthToken(
      /*[in] */ const std::string& tokenUrl,
      /*[in] */ const std::string& clientId,
      /*[in] */ const std::string& clientSecret,
      /*[out]*/ AdlsAuthToken& authToken);

    void ReadChunk(
      /*[in] */ const AdlsAuthToken& authToken,
      /*[in] */ const std::string& remoteFilePath,
      /*[out]*/ std::ostream& dataSink,
      /*[in] */ size_t size,
      /*[in] */ off_t offset);

    std::string ListDirectory(
      /*[in] */ const AdlsAuthToken& authToken,
      /*[in] */ const std::string& remoteDirPath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries);

    int GetFileOrDirectoryStatus(
      /*[in] */ const AdlsAuthToken& authToken,
      /*[in] */ const std::string& remotePath,
      /*[out]*/ FileAttributes& attr);

  public:
    /**
     * @brief Create client
     * @param host %ADLS service hostname
     */
    AdlsHttpClient(const std::string& host);

    ~AdlsHttpClient();

    AdlsHttpClient(const AdlsHttpClient &) = delete;

    AdlsHttpClient &operator=(const AdlsHttpClient &) = delete;

    AdlsHttpClient(AdlsHttpClient &&);

    AdlsHttpClient& operator=(AdlsHttpClient &&);

  private:
    /* Helper method */
    bool TryParseJson(
      /*[in]*/ const std::string& jsonStr,
      /*[out]*/ Json::Value& parsedJson);

  private:
    void                                    InitHttpClient(/*[in]*/ bool reuseConnection = true);

  private:
    std::unique_ptr<CurlHttpClient>         m_spHttpClient;
    std::unique_ptr<AdlsHttpUrlBuilder>     m_spUrlBuilder;
    static constexpr int                    m_nConnectTimeoutSec = 20;
    static constexpr int                    m_nDataTransferTimeoutSec = 60;
};
//---------------------------------------------------------------------------------------------------------------------
