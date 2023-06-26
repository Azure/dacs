//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include <stdlib.h>
#include "SnapshotManagerTest.h"
#include "SnapshotManager.h"
#include "Constants.h"
#include "Utils.h"
//---------------------------------------------------------------------------------------------------------------------
SnapshotManagerTest::SnapshotManagerTest()
{
    // Set environment variables
    ::setenv("CACHESERVER_SNAPSHOT_ENABLED", "true", 1);
    ::setenv("CACHESERVER_HOSTNAME", "unittesthost", 1);

    SetCacheRootPath("/tmp/snapshotManagerUnitTest");
    InitializeUtils();
}
//---------------------------------------------------------------------------------------------------------------------
void SnapshotManagerTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<snapshot::Snapshot> SnapshotManagerTest::CreateMockSnapshot()
{
    // Create mock snapshot
    std::shared_ptr<snapshot::Snapshot> pSnapshot = std::make_shared<snapshot::Snapshot>();
    pSnapshot->set_version(SNAPSHOT_VERSION);
    pSnapshot->set_hostname("unittesthost");
    pSnapshot->set_savetimestamp(time(NULL) - 600);
    for (int bucketIdx = 0; bucketIdx < NUM_MAPS; bucketIdx++)
    {
        snapshot::SnapshotBucket* pBucket = pSnapshot->add_buckets();
        for (int entryIdx = 0; entryIdx < 10; entryIdx++)
        {
            snapshot::SnapshotEntry* pEntry = pBucket->add_entries();
            pEntry->set_filename("file" + std::to_string(entryIdx));
            pEntry->set_filesize(1024 * entryIdx);

            auto& entryMetadata = *(pEntry->mutable_metadata());
            entryMetadata["filename"] = "file" + std::to_string(entryIdx);

            if (entryIdx % 2 == 0)
            {
                pEntry->set_expirytimestamp(1000 * entryIdx);
            }
            else
            {
                pEntry->set_groupid("group" + std::to_string(entryIdx));
            }
        }
    }

    return pSnapshot;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, SaveAndLoadSnapshot)
{
    // Create mock snapshot
    std::string fileName = "/tmp/snapshot";
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot();

    SnapshotManager::SaveSnapshotToFile(fileName, *pSnapshot);
    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
    ASSERT_NE(pLoadedSnapshot, nullptr);
    ASSERT_EQ(pLoadedSnapshot->version(), SNAPSHOT_VERSION);
    ASSERT_EQ(pLoadedSnapshot->buckets_size(), NUM_MAPS);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, LoadMissingSnapshot)
{
    std::shared_ptr<snapshot::Snapshot> pSnapshot = SnapshotManager::LoadSnapshotFromFile("/tmp/missingSnapshot");
    ASSERT_EQ(pSnapshot, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, LoadDifferentVersionSnapshot)
{
    // Create mock snapshot
    std::string fileName = "/tmp/diffVersionSnapshot";
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot();
    pSnapshot->set_version(SNAPSHOT_VERSION + 1);

    SnapshotManager::SaveSnapshotToFile(fileName, *pSnapshot);
    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
    ASSERT_EQ(pLoadedSnapshot, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, LoadDifferentHostnameSnapshot)
{
    // Create mock snapshot
    std::string fileName = "/tmp/diffHostnameSnapshot";
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot();
    pSnapshot->set_hostname("diffHostname");

    SnapshotManager::SaveSnapshotToFile(fileName, *pSnapshot);
    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
    ASSERT_EQ(pLoadedSnapshot, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, LoadExpiredSnapshot)
{
    // Create mock snapshot
    std::string fileName = "/tmp/expiredSnapshot";
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot();
    pSnapshot->set_savetimestamp(time(NULL) - 10 * 24 * 60 * 60);

    SnapshotManager::SaveSnapshotToFile(fileName, *pSnapshot);
    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
    ASSERT_EQ(pLoadedSnapshot, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------