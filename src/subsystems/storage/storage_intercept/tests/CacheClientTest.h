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
#include "TestBase.h"
#include "FileData.h"
#include <string>
#include <atomic>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class ICacheClient;
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTest : public TestBase
{
    protected:
        CacheClientTest();
        void SetUp() override;

        std::shared_ptr<ICacheClient>   m_cacheClient;
        static std::atomic_int          m_fileCount;
        std::string                     m_strTestId;
        bool                            m_bRunExtensiveTests;

        std::string GetRandFilename();

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