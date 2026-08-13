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