//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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