//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "TestBase.h"
#include "FileReadData.h"
#include <string>
#include <atomic>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class ICacheClient;
//---------------------------------------------------------------------------------------------------------------------
class LocalFSCacheClientTest : public TestBase
{
    protected:
        LocalFSCacheClientTest();
        void SetUp() override;

        std::shared_ptr<ICacheClient>   m_cacheClient;
        static std::atomic_int          m_fileCount;
        std::string                     m_strTestId;

        std::string GetRandFilename();
        std::shared_ptr<FileReadData> GetRandDataBuffer(uint64_t size);

        template <typename T>
        void AssertEqArray(T* left, T* right, int arraySize)
        {
            for (int i = 0; i < arraySize; i++)
            {
                ASSERT_EQ(left[i], right[i]);
            }
        }
};
//---------------------------------------------------------------------------------------------------------------------