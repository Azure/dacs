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
#include "StorageInterceptLogger.h"
#include <memory>
#include "CurlHandle.h"
#include <curl/curl.h>
#include "RemoteError.h"
#include "CurlConnectionManager.h"
#include "CurlClientException.h"
#include "Request.h"
#include "Reply.h"
#include "ReplyHandler.h"
//---------------------------------------------------------------------------------------------------------------------
class CurlHttpClient
{
  public:
    CurlHttpClient(bool reuseConnection);
    ~CurlHttpClient();

  public:
    void SetConnectionTimeout(/*[in]*/ int seconds);

    void SetDataTransferTimeout(/*[in]*/ int seconds);

    Reply Make(/*[in]*/ const Request& req);

    bool TryParseRemoteError(/*[in]*/ const std::string &s, /*[out]*/ RemoteError &remoteError);

  private:
    void SetHttpHeaders(
      /*[in]*/ const AdlsAuthToken& authToken,
      /*[in]*/ const std::vector<std::string> &headers = std::vector<std::string>());

    static size_t StreamReadCallback(
      /*[out]*/ char *buffer,
      /*[in] */ size_t size,
      /*[in] */ size_t nitems,
      /*[in] */ void *userdata);

  private:
    std::unique_ptr<CurlHandle>             m_curlHandle;
    CURL*                                   m_curl; /* just a raw ptr handled by m_curlHandle */
    std::unique_ptr<curl_slist>             m_activeHttpHeaders; /* we must handle allocated headers */
    bool                                    m_bReuseConnection;
};
//---------------------------------------------------------------------------------------------------------------------