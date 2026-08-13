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
#include "AdlsAuthToken.h"
#include <map>
//---------------------------------------------------------------------------------------------------------------------
struct Request
{
  enum class Type
  {
    GET,
    PUT,
    POST,
    DELETE
  };

  Type                                      type = Type::GET;
  std::string                               url;
  AdlsAuthToken                             authToken;
  bool                                      followRedirect = false;
  std::ostream                              *pDataSink = nullptr;
  std::istream                              *pDataSource = nullptr;
  long                                      expectedResponseCode = 0L;
  std::map<std::string, std::string>        multipartForm;
};
//---------------------------------------------------------------------------------------------------------------------