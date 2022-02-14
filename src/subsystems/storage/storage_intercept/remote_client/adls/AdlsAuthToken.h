//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <sys/types.h>
//---------------------------------------------------------------------------------------------------------------------
struct AdlsAuthToken
{
  AdlsAuthToken()
  {
    type = "";
    authToken = "";
    expiryTime = 0;
  }

  std::string type;
  std::string authToken;
  size_t expiryTime;

  /* Useful to check if this token is valid / expired */
  bool isTokenValid(size_t minExpiryTime) const
  {
    return (authToken != "" && type != "" && expiryTime >= minExpiryTime);
  }
};
//---------------------------------------------------------------------------------------------------------------------