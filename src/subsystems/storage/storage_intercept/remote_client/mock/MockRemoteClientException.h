///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include "IRemoteClientException.h"
//---------------------------------------------------------------------------------------------------------------------
class MockRemoteClientException : IRemoteClientException
{
  public:
    MockRemoteClientException(/*[in] */ const std::string& exceptionStr)
      : m_strError(exceptionStr)
    {}

    std::string GetErrorMessage() const override
    {
      return m_strError;
    }

    bool IsRetriableError() const override
    {
      return false;
    }

  private:
    const std::string m_strError;
};
//---------------------------------------------------------------------------------------------------------------------