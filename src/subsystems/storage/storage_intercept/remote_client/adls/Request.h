//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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