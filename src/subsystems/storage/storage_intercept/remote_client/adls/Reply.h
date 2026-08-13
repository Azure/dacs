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
