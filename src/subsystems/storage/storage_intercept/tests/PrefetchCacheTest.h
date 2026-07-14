//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include <string>
#include "FileData.h"
//---------------------------------------------------------------------------------------------------------------------
class PrefetchCacheTest : public TestBase
{
    protected:
        PrefetchCacheTest();
        void SetUp() override;

        std::shared_ptr<FileData> GetRandDataBuffer(off64_t offset, uint64_t size);

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