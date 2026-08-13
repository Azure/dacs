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

#include "LockManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
LockManager::LockManager()
: m_vLockedFilesSet(NUM_MAPS),
  m_vLockedFilesSetMutex(NUM_MAPS)
{
}
//---------------------------------------------------------------------------------------------------------------------
LockManager::~LockManager()
{
}
//---------------------------------------------------------------------------------------------------------------------
LockManager& LockManager::GetInstance()
{
    static LockManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
bool LockManager::GetLockForUpload(const std::string& fileName)
{
    auto index = GetHashBucket(fileName);

    bool ownFile = false;
    lock_guard lockGuard(m_vLockedFilesSetMutex[index]);
    if (m_vLockedFilesSet[index].find(fileName) == m_vLockedFilesSet[index].end())
    {
        m_vLockedFilesSet[index].insert(fileName);
        ownFile = true;
    }
    return ownFile;
}
//---------------------------------------------------------------------------------------------------------------------