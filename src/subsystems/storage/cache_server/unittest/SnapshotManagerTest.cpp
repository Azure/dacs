//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include <stdlib.h>
#include "SnapshotManagerTest.h"
#include "SnapshotManager.h"
#include "Constants.h"
#include "Utils.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std::experimental::filesystem;
using namespace google::protobuf::util;
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
std::shared_ptr<snapshot::Snapshot> SnapshotManagerTest::CreateMockSnapshot(std::size_t numEntriesPerBucket)
{
    // Create mock snapshot
    std::shared_ptr<snapshot::Snapshot> pSnapshot = std::make_shared<snapshot::Snapshot>();
    pSnapshot->set_version(SNAPSHOT_VERSION);
    pSnapshot->set_hostname("unittesthost");
    pSnapshot->set_savetimestamp(time(NULL) - 600);
    for (int bucketIdx = 0; bucketIdx < NUM_MAPS; bucketIdx++)
    {
        snapshot::SnapshotBucket* pBucket = pSnapshot->add_buckets();
        for (int entryIdx = 0; entryIdx < numEntriesPerBucket; entryIdx++)
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
/** Either snapshot is saved or not saved, but no invalid / partial snapshot. **/
// fork() behavoir is broken in gtest for Ubuntu 24.04, so disable this test
TEST_F(SnapshotManagerTest, DISABLED_CrashWhileTakingSnapshot)
{
    std::string fileName = "/tmp/snapshot";
    SnapshotManager snapshotManager = SnapshotManager::GetInstance();
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot(1000);

    auto startTs = std::chrono::high_resolution_clock::now();
    snapshotManager.SaveSnapshotToFile(fileName, *pSnapshot);
    auto endTS = std::chrono::high_resolution_clock::now();
    auto saveDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTS - startTs);

    LOG_INFO("Starting loop to catch race-conditions.");
    for(int sleepMs = saveDuration.count() / 2; sleepMs < saveDuration.count() * 2; sleepMs += 5)
    {
        pid_t pid = fork();
        if(pid == 0)
        {
            // In child
            snapshotManager.SaveSnapshot(*pSnapshot);
        }
        else if(pid > 0)
        {
            // In parent, pid is child pid
            // Crash the child
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            kill(pid, SIGKILL);
            if(std::experimental::filesystem::exists(fileName))
            {
                std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
                ASSERT_NE(pLoadedSnapshot, nullptr);
                ASSERT_TRUE(MessageDifferencer::Equals(*pLoadedSnapshot, *pSnapshot));
            }
        }
        else
        {
            FAIL();
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, ReadCorruptedSnapshot)
{
    std::string fileName = "/tmp/corruptedSnapshot";

    // Write random string to file
    std::ofstream ofs(fileName, std::ios::binary);
    ofs << "this is not a valid protobuf message file";
    ofs.close();

    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot;
    EXPECT_NO_THROW(pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName));
    ASSERT_EQ(pLoadedSnapshot, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SnapshotManagerTest, HandleLargeSnapshotFile)
{
    std::string fileName = "/tmp/snapshot";

    // This test can take a few seconds.
    std::shared_ptr<snapshot::Snapshot> pSnapshot = CreateMockSnapshot(50000);
    std::cout << "Snapshot size (MB) is " << pSnapshot->ByteSizeLong() / (1024 * 1024) << std::endl;
    SnapshotManager::SaveSnapshotToFile(fileName, *pSnapshot);

    std::shared_ptr<snapshot::Snapshot> pLoadedSnapshot = SnapshotManager::LoadSnapshotFromFile(fileName);
    std::cout << "Loaded snapshot size (MB) is " << pLoadedSnapshot->ByteSizeLong() / (1024 * 1024) << std::endl;
    ASSERT_TRUE(MessageDifferencer::Equals(*pSnapshot, *pLoadedSnapshot));
}
//---------------------------------------------------------------------------------------------------------------------