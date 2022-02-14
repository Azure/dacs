//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "Reply.h"
#include <iostream>
#include <curl/curl.h>
//---------------------------------------------------------------------------------------------------------------------
struct ReplyHandler
{
  static size_t WriteCallback(char *buffer, size_t size, size_t nitems, void *userData)
  {
    auto self = static_cast<ReplyHandler *>(userData);
    auto &reply = self->reply;
    auto pDataSink = self->pDataSink;
    const auto dataSize = size * nitems;

    if (reply.responseCode == 0 &&
      curl_easy_getinfo(self->curl, CURLINFO_RESPONSE_CODE, &reply.responseCode) != CURLE_OK)
    {
      reply.responseCode = Reply::RESPONSE_CODE_CLIENT_ERROR;
      reply.clientError = "libcurl getinfo failed";
      return 0;
    }

    if (self->expectedResponseCodes != reply.responseCode)
    {
      reply.unexpectedResponseContent.append(buffer, dataSize);
    }
    else if (pDataSink != nullptr)
    {
      pDataSink->write(buffer, dataSize);
      if (!pDataSink->good())
      {
        return 0;
      }
    }
    return dataSize;
  }

  Reply                             &reply;
  const long                        expectedResponseCodes;
  std::ostream                      *pDataSink;
  CURL                              *curl;
};
//---------------------------------------------------------------------------------------------------------------------