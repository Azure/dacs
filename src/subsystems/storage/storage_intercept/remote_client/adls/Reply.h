//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------------------------------------------------
struct Reply
{
  static const long                   RESPONSE_CODE_CLIENT_ERROR = -1L; // special code to indicate client error
  long                                responseCode = 0L;
  std::string                         unexpectedResponseContent; // for error or other unexpected reply
  std::string                         clientError;               // to put an error occured in callback
  std::string                         redirectUrl;
};
//---------------------------------------------------------------------------------------------------------------------
