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
#include "cache.pb.h"
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
//---------------------------------------------------------------------------------------------------------------------
class LockManager
{
    public:
        static LockManager& GetInstance();

        // Lock the file for upload, used by clients to coordinate and ensure only one client uploads a deduped file
        bool GetLockForUpload(
            /*[in] */ const std::string& fileName);

    private:
        LockManager();
        ~LockManager();

        // Bucketized lock map
        std::vector<std::unordered_set<std::string>> m_vLockedFilesSet;

        // Mutexes to synchronize the buckets
        std::vector<std::mutex> m_vLockedFilesSetMutex;
};
//---------------------------------------------------------------------------------------------------------------------