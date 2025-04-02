//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Base class for all remote API operations. It contains the common retry logic.
 *
 * All remote API operations should derive from this and override the virtual methods.
 * Invoking Execute() on derived class would then perform
 */
class RemoteClientApiBase
{
  public:
    RemoteClientApiBase(
      /*[in] */ const std::string& operationName,
      /*[in] */ uint16_t maxRetryCount = 5,
      /*[in] */ uint16_t initialRetrySleepInMs = 100);

    virtual ~RemoteClientApiBase() = default;

    void Execute();

  protected:
    virtual void PerformOperation() = 0;
    const std::string               m_strOperationName;

  private:
    const uint16_t                  m_nMaxRetryCount;
    const uint16_t                  m_nInitialRetrySleepInMs;
};
//---------------------------------------------------------------------------------------------------------------------