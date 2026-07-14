//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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