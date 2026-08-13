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
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <vector>
#include "DirectoryEntry.h"
#include "RemoteClientApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class MockReadDirectoryApi : public RemoteClientApiBase
{
    public:
        MockReadDirectoryApi(
            /*[in] */ const std::string& dirPath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries);

        void PerformOperation() override;

        std::string GetResult() const;

    private:
        // Inputs parameters
        const std::string m_strDirPath;
        const std::string m_strContToken;
        std::vector<DirectoryEntry>& m_vecDirEntries;

        // API result (new continuation token)
        std::string m_strNewContToken;
};
//---------------------------------------------------------------------------------------------------------------------