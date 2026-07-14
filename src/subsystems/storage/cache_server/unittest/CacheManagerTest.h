//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
class CacheManagerTest : public ::testing::Test
{
    protected:
        CacheManagerTest();

        void SetUp() override;

        void AddCacheEntry(
            const std::string& fileName,
            uint64_t fileSize,
            time_t expiryTime,
            const std::string& groupId);

        void ValidateCacheEntry(
            const std::string& fileName,
            uint64_t fileSize,
            time_t expiryTime,
            const std::string& groupId);

        void ValidateCacheEntryNotExists(const std::string& fileName);
};
//---------------------------------------------------------------------------------------------------------------------