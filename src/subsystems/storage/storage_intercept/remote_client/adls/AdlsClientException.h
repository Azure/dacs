/// Copyright (c) Microsoft Corporation.
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
#include "../IRemoteClientException.h"
#include "AdlsHttpClientException.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsClientException : public IRemoteClientException
{
  public:
    AdlsClientException(AdlsHttpClientException exception)
      : m_Error(exception.what())
    {}

    std::string GetErrorMessage() const override
    {
      return m_Error;
    }

    bool IsRetriableError() const override
    {
      return true; // TODO: should be true only for timeouts, etc
    }

  private:
    std::string m_Error;
};
//---------------------------------------------------------------------------------------------------------------------
