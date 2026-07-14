///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include <fstream>
#include <stdlib.h>
#include <fcntl.h>
#include "Constants.h"
#include "Logger.h"
#include "Utils.h"
#include "SnapshotManager.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace snapshot;
using namespace std;
using namespace google::protobuf::io;
//---------------------------------------------------------------------------------------------------------------------
SnapshotManager::SnapshotManager()
    : m_snapshotEnabled(DefaultSnapshotEnabled)
    , m_waitForSnapshotConsistencyScan(DefaultWaitForSnapshotConsistencyScan)
    , m_snapshotIntervalInSeconds(DefaultSnapshotIntervalInSeconds)
{
    char* snapshotEnabled = std::getenv("CACHESERVER_SNAPSHOT_ENABLED");
    if (snapshotEnabled != nullptr)
    {
        m_snapshotEnabled = (strcmp(snapshotEnabled, "true") == 0);
    }

    string serverHostName = GetServerHostName();
    if (serverHostName.empty())
    {
        // Server hostname has been made mandatory to enable snapshot so that we can detect the case of a server
        // partition starting on a node previously hosting some other server partition and not load snapshot data for
        // that case.
        m_snapshotEnabled = false;
        LOG_INFO("Snapshot disabled as server hostname is not set");
    }

    char* waitForSnapshotConsistencyScan = std::getenv("CACHESERVER_WAIT_FOR_SNAPSHOT_CONSISTENCY_SCAN");
    if (waitForSnapshotConsistencyScan != nullptr)
    {
        m_waitForSnapshotConsistencyScan = (strcmp(waitForSnapshotConsistencyScan, "true") == 0);
    }

    char* snapshotInterval = std::getenv("CACHESERVER_SNAPSHOT_INTERVAL");
    if (snapshotInterval != nullptr)
    {
        m_snapshotIntervalInSeconds = std::stoi(snapshotInterval);
    }

    LOG_INFO("Initialized snapshot manager. SnapshotEnabled: {}, ServerHostname: {}, SnapshotIntervalInSeconds: {}",
        m_snapshotEnabled,
        serverHostName,
        m_snapshotIntervalInSeconds);
}
//---------------------------------------------------------------------------------------------------------------------
SnapshotManager::~SnapshotManager()
{
}
//---------------------------------------------------------------------------------------------------------------------
const string SnapshotManager::CurrentSnapshotFileName = "current.snapshot";
const string SnapshotManager::TempSnapshotFileName = "temp.snapshot";
//---------------------------------------------------------------------------------------------------------------------
SnapshotManager& SnapshotManager::GetInstance()
{
    static SnapshotManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
bool SnapshotManager::IsSnapshotEnabled() const
{
    return m_snapshotEnabled;
}
//---------------------------------------------------------------------------------------------------------------------
bool SnapshotManager::WaitForSnapshotConsistencyScan() const
{
    return m_waitForSnapshotConsistencyScan;
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t SnapshotManager::GetSnapshotIntervalInSeconds() const
{
    return m_snapshotIntervalInSeconds;
}
//---------------------------------------------------------------------------------------------------------------------
/* static */ shared_ptr<Snapshot> SnapshotManager::LoadSnapshotFromFile(const string& fileName)
{
    LOG_INFO("Loading snapshot file: {}", fileName);
    TIMER_START(loadSnapshotFile);
    shared_ptr<Snapshot> snapshot = make_shared<Snapshot>();

    ifstream input(fileName, ios::in | ios::binary);
    if (!input)
    {
        LOG_INFO("Snapshot file not found: {}", fileName);
        return nullptr;
    }

    IstreamInputStream rawInput(&input);
    CodedInputStream codedStream(&rawInput);

    LOG_INFO("Setting coded stream total bytes limit to: {}", INT_MAX);
    codedStream.SetTotalBytesLimit(INT_MAX);

    if(!snapshot->ParseFromCodedStream(&codedStream))
    {
        LOG_WARN("Failed to parse snapshot file: {}", fileName);
        return nullptr;
    }

    if (snapshot->version() != SNAPSHOT_VERSION)
    {
        LOG_WARN("Snapshot version mismatch. snapshot: {} expected {}", snapshot->version(), SNAPSHOT_VERSION);
        return nullptr;
    }

    string serverHostName = GetServerHostName();
    if (snapshot->hostname() != serverHostName)
    {
        LOG_WARN("Snapshot hostname mismatch. snapshot: {} expected {}", snapshot->hostname(), serverHostName);
        return nullptr;
    }

    time_t now = time(NULL);
    if (snapshot->savetimestamp() < now - SNAPSHOT_MAX_AGE_IN_SECONDS)
    {
        LOG_WARN("Snapshot too old. snapshot: {} now: {}", snapshot->savetimestamp(), now);
        return nullptr;
    }

    if (snapshot->buckets_size() != NUM_MAPS)
    {
        LOG_WARN("Snapshot bucket count mismatch. snapshot: {} expected {}", snapshot->buckets_size(), NUM_MAPS);
        return nullptr;
    }

    LOG_INFO("Loaded snapshot file: {}", fileName);
    TIMER_STOP_AND_LOG(loadSnapshotFile);
    return snapshot;
}
//---------------------------------------------------------------------------------------------------------------------
void SnapshotManager::SaveSnapshotToFile(const string& fileName, const Snapshot& snapshot)
{
    LOG_INFO("Saving snapshot file: {}", fileName);
    TIMER_START(saveSnapshotFile);

    int fd = open(fileName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        LOG_ERROR("Failed to open snapshot file for writing: {}. errno: {}:{}", fileName, errno, strerror(errno));
        return;
    }

    if (!snapshot.SerializeToFileDescriptor(fd))
    {
        LOG_ERROR("Failed to write snapshot file: {}", fileName);
        return;
    }

    if (fsync(fd) != 0)
    {
        LOG_ERROR("Failed to fsync snapshot file: {} errno: {}:{}", fileName, errno, strerror(errno));
        return;
    }

    if (close(fd) != 0)
    {
        LOG_ERROR("Failed to close snapshot file: {} errno: {}:{}", fileName, errno, strerror(errno));
        return;
    }

    LOG_INFO("Saved snapshot file: {}", fileName);
    TIMER_STOP_AND_LOG(saveSnapshotFile);
}
//---------------------------------------------------------------------------------------------------------------------
shared_ptr<Snapshot> SnapshotManager::LoadSnapshotOnStartup()
{
    STORAGE_ASSERT(m_snapshotEnabled, "Snapshot load called when snapshot is not enabled");

    TIMER_START(loadSnapshotOnStartup);
    const std::string CurrentSnapshotFilePath = GetSnapshotFolderPath() + "/" + CurrentSnapshotFileName;
    shared_ptr<Snapshot> pSnapshot = LoadSnapshotFromFile(CurrentSnapshotFilePath);
    if (pSnapshot == nullptr)
    {
        LOG_INFO("Current snapshot file could not be loaded. No snapshot available.")
        return nullptr;
    }

    LOG_INFO("Loaded current snapshot file: {}", CurrentSnapshotFilePath);
    TIMER_STOP_AND_LOG(loadSnapshotOnStartup);
    return pSnapshot;
}
//---------------------------------------------------------------------------------------------------------------------
void SnapshotManager::SaveSnapshot(const Snapshot& snapshot)
{
    STORAGE_ASSERT(m_snapshotEnabled, "Snapshot save called when snapshot is not enabled");
    STORAGE_ASSERT(snapshot.version() == SNAPSHOT_VERSION,
      "Snapshot version mismatch. snapshot: {} expected {}", snapshot.version(), SNAPSHOT_VERSION);
    STORAGE_ASSERT(snapshot.hostname() == GetServerHostName(),
      "Snapshot hostname mismatch. snapshot: {} expected {}", snapshot.hostname(), GetServerHostName());

    TIMER_START(saveSnapshot);
    const std::string TempSnapshotFilePath = GetSnapshotFolderPath() + "/" + TempSnapshotFileName;
    LOG_INFO("Saving snapshot to temp file: {}", TempSnapshotFilePath);
    SaveSnapshotToFile(TempSnapshotFilePath, snapshot);

    const std::string CurrentSnapshotFilePath = GetSnapshotFolderPath() + "/" + CurrentSnapshotFileName;
    LOG_INFO("Renaming temp snapshot {} to current snapshot {}", TempSnapshotFilePath, CurrentSnapshotFilePath);
    rename(TempSnapshotFilePath.c_str(), CurrentSnapshotFilePath.c_str());

    LOG_INFO("Saved snapshot");
    TIMER_STOP_AND_LOG(saveSnapshot);
}
//---------------------------------------------------------------------------------------------------------------------