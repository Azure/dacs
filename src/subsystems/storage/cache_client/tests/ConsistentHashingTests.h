//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <gtest/gtest.h>
#include <vector>
#include <string>

#include "CacheClientImpl.h"

//---------------------------------------------------------------------------------------------------------------------
class ConsistentHashingTests : public ::testing::Test
{
private:
    static constexpr uint64_t seed = 42; // Fixed seed for reproducible tests
protected:
    static constexpr const char* CONSISTENT_HASHING_ENV_VAR = "SI_CACHEENABLECONSISTENTHASHING";
    
    // Helper methods for test setup and utilities
    void InitializeClient(CacheClientImpl& client);
    std::vector<std::string> GenerateKeys(int count, const std::string& prefix = "testkey");
};
//---------------------------------------------------------------------------------------------------------------------