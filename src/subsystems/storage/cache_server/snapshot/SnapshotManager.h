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
#include "snapshot.pb.h"
#include <string>
#include <memory>
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
class SnapshotManagerTest;
//---------------------------------------------------------------------------------------------------------------------
class SnapshotManager
{
    // Friends for testing
    FRIEND_TEST(SnapshotManagerTest, SaveAndLoadSnapshot);
    FRIEND_TEST(SnapshotManagerTest, LoadMissingSnapshot);
    FRIEND_TEST(SnapshotManagerTest, LoadDifferentVersionSnapshot);
    FRIEND_TEST(SnapshotManagerTest, LoadDifferentHostnameSnapshot);
    FRIEND_TEST(SnapshotManagerTest, LoadExpiredSnapshot);
    FRIEND_TEST(SnapshotManagerTest, DISABLED_CrashWhileTakingSnapshot);
    FRIEND_TEST(SnapshotManagerTest, ReadCorruptedSnapshot);
    FRIEND_TEST(SnapshotManagerTest, HandleLargeSnapshotFile);

    public:
        static SnapshotManager& GetInstance();

        bool IsSnapshotEnabled() const;

        bool WaitForSnapshotConsistencyScan() const;

        uint32_t GetSnapshotIntervalInSeconds() const;

        std::shared_ptr<snapshot::Snapshot> LoadSnapshotOnStartup();

        void SaveSnapshot(const snapshot::Snapshot& snapshot);

    private:
        SnapshotManager();
        ~SnapshotManager();

        bool m_snapshotEnabled;
        bool m_waitForSnapshotConsistencyScan;
        uint32_t m_snapshotIntervalInSeconds;

        static const std::string CurrentSnapshotFileName;
        static const std::string TempSnapshotFileName;

        static std::shared_ptr<snapshot::Snapshot> LoadSnapshotFromFile(const std::string& fileName);

        static bool SaveSnapshotToFile(const std::string& fileName, const snapshot::Snapshot& snapshot);
};
//---------------------------------------------------------------------------------------------------------------------