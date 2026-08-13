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