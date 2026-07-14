///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
