/// Copyright (c) Microsoft Corporation.
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
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include "AdlsApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class AdlsReadChunkApi : public AdlsApiBase
{
  public:
    AdlsReadChunkApi(
      /*[in] */ const AdlsHostContext& hostContext,
      /*[in] */ const std::string& filePath,
      /*[out]*/ void *buf,
      /*[in] */ size_t length,
      /*[in] */ off_t offset);

  public:
    void PerformAdlsOperation() override;

    ssize_t GetResult() const;

  private:
    /* Api parameters */
    const std::string               m_strFilePath;
    void* const                     m_pBuf;
    const size_t                    m_nReadLength;
    const off_t                     m_nReadOffset;

    /* Api result */
    ssize_t                         m_nBytesRead;
};
//---------------------------------------------------------------------------------------------------------------------