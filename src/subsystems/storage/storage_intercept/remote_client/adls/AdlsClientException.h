///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
