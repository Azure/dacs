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
class AdlsReadDirectoryApi : public AdlsApiBase
{
  public:
    AdlsReadDirectoryApi(
      /*[in] */ const AdlsHostContext& hostContext,
      /*[in] */ const std::string& remotePath,
      /*[in] */ const std::string& continuationToken,
      /*[out]*/ std::vector<DirectoryEntry>& dirEntries);
  
  public:
    void PerformAdlsOperation() override;

    std::string GetResult() const;

  private:
    /* Api parameters */
    const std::string               m_strRemotePath;
    const std::string               m_strContinuationToken;
    std::vector<DirectoryEntry>&    m_vecDirEntries;

    /* Api result */
    std::string                     m_strResultContinuationToken;
};
//---------------------------------------------------------------------------------------------------------------------