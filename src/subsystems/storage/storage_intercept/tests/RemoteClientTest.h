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
#include <IRemoteClient.h>
//---------------------------------------------------------------------------------------------------------------------
class RemoteClientTest : public TestBase
{
    protected:
        RemoteClientTest();
        void SetUp() override;

        template<typename Base, typename T>
        bool IsInstanceOf(const T* pType);

        std::shared_ptr<IRemoteClient> m_RemoteClient;
        const std::string DIR_DELIMITER;
};
//---------------------------------------------------------------------------------------------------------------------