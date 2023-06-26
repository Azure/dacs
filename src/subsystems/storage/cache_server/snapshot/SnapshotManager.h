///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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

        static void SaveSnapshotToFile(const std::string& fileName, const snapshot::Snapshot& snapshot);
};
//---------------------------------------------------------------------------------------------------------------------