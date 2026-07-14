///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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