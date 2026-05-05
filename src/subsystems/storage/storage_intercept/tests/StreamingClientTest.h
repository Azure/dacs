//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include "StreamingClient.h"
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

        void PerformReadTest(
            /*[in]*/ size_t bufferSize,
            /*[in]*/ size_t offset,
            /*[in]*/ ssize_t expectedBytesRead,
            /*[in]*/ int expectedZeroCopyReads);
        
        void WriteAppendsTest(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ std::shared_ptr<FileData> writeData,
            /*[in]*/ size_t appendSize);

        void EnsureSubChunkConfig();

        std::vector<char>                   m_vecBuffer;
        std::shared_ptr<ICacheClient>       m_cacheClient;
        std::shared_ptr<StreamingClient>    m_spStreamingClient;
};
//---------------------------------------------------------------------------------------------------------------------