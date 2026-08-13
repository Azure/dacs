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
        std::string                         m_strFileVersion;
};
//---------------------------------------------------------------------------------------------------------------------