//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
//---------------------------------------------------------------------------------------------------------------------
class StreamingClientTest : public TestBase
{
    protected:
        StreamingClientTest();
        void SetUp() override;
        bool BufferEquals(
            /*[in]*/ const std::vector<char>& vecFirst,
            /*[in]*/ const std::vector<char>& vecSecond);

        bool BufferEqualsInRange(
            /*[in]*/ const std::vector<char>& vecFirst,
            /*[in]*/ const std::vector<char>& vecSecond,
            /*[in]*/ size_t nStartIdxFirst,
            /*[in]*/ size_t nStartIdxSecond,
            /*[in]*/ size_t nSize);

        std::vector<char>       m_vecBuffer;
};
//---------------------------------------------------------------------------------------------------------------------