///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
//---------------------------------------------------------------------------------------------------------------------
constexpr int DEFAULT_THREAD_POOL_SIZE = 256;
constexpr int TCP_BACKLOG_SIZE = 1024;
constexpr int REQUEST_METADATA_SIZE = 4;
constexpr int NUM_MAPS = 50; // Note: changing this value will cause existing snapshots to fail to load

// Snapshot related settings
constexpr bool DefaultSnapshotEnabled = true;
constexpr bool DefaultWaitForSnapshotConsistencyScan = true;
constexpr uint32_t DefaultSnapshotIntervalInSeconds = 300;
constexpr uint32_t SNAPSHOT_VERSION = 1; // Note: Update this only if breaking changes are made to snapshot proto
constexpr uint32_t SNAPSHOT_MAX_AGE_IN_SECONDS = 7 * 24 * 60 * 60; // 1 week

// Window size for cache expiry cleanup
constexpr uint32_t DefaultCacheTTLWindowSizeSeconds = 60;

// Eviction related settings
constexpr uint64_t DefaultEvictionLastAccessThresholdSeconds = 2592000; // 30 days
constexpr uint32_t DefaultEvictionPassFrequencySeconds = 3630; // ~ 1hr

// 1TB cache size
constexpr uint64_t DefaultCacheSizeLimitBytes = 1024ull * 1024 * 1024 * 1024; 

// Limit to contain RAM usage
constexpr uint64_t DefaultAttributeCacheLimit = 10 * 1024 * 1024;

// 4GB limit on individual file size 
constexpr uint64_t UPLOAD_FILESIZE_LIMIT = (uint64_t)4 * 1024 * 1024 * 1024;

// Linux has filename limit of 255, due to hex encoding we can only support source filenames of length 127
constexpr int UPLOAD_FILENAME_LIMIT = 127;
//---------------------------------------------------------------------------------------------------------------------