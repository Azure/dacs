///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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