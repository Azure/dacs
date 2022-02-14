///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptLogger.h"
#include "CurlHandle.h"
#include <memory>
#include <queue>
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
class CurlConnectionManager
{
  public:
    static CurlConnectionManager&           GetInstance();
    ~CurlConnectionManager();

  public:
    std::unique_ptr<CurlHandle>             GetCurlHandle();
    void                                    ReleaseCurlHandle(/*[in]*/ std::unique_ptr<CurlHandle>&& curlHandle);

  private:
    std::mutex                              m_connectionPoolLock;
    std::queue<std::unique_ptr<CurlHandle>> m_queueHandles;
    static size_t                           m_nMaxHandles;
};
//---------------------------------------------------------------------------------------------------------------------