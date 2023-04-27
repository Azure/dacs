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
constexpr int NUM_MAPS = 50;

// Window size for cache expiry cleanup
constexpr uint32_t DefaultCacheTTLWindowSizeSeconds = 60;

// 1TB cache size
constexpr uint64_t DefaultCacheSizeLimitBytes = 1024ull * 1024 * 1024 * 1024; 

// Limit to contain RAM usage
constexpr uint64_t DefaultAttributeCacheLimit = 10 * 1024 * 1024;

// 4GB limit on individual file size 
constexpr uint64_t UPLOAD_FILESIZE_LIMIT = (uint64_t)4 * 1024 * 1024 * 1024;

// Linux has filename limit of 255, due to hex encoding we can only support source filenames of length 127
constexpr int UPLOAD_FILENAME_LIMIT = 127;
//---------------------------------------------------------------------------------------------------------------------