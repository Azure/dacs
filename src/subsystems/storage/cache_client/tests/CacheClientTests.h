//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <gtest/gtest.h>
#include <memory>

#include "CacheClientImpl.h"
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTests : public ::testing::TestWithParam<bool>
{
  protected:
    void SetUp() override;
    void TearDown() override;
    const char* GetRandomBuffer(const uint64_t nLength) const;
    void SetCacheServerCount(int count) const;
    static void InitializeClient(CacheClientImpl& client, bool bIsDiscoveryEnabled = false, bool bEnableCalculateChecksum = false, bool bEnableValidateChecksum = false);

    CacheClientImpl m_cacheClientImpl;
    CacheClientImpl m_cacheClientImplCRC;
};
//---------------------------------------------------------------------------------------------------------------------