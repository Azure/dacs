///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
//---------------------------------------------------------------------------------------------------------------------
constexpr int THREAD_POOL_SIZE = 128;
constexpr int TCP_BACKLOG_SIZE = 1000;
constexpr int REQUEST_METADATA_SIZE = 4;
constexpr int NUM_MAPS = 50;

// 1TB cache size
constexpr uint64_t DefaultCacheSizeLimitBytes = 1024ull * 1024 * 1024 * 1024; 

// Limit to contain RAM usage
constexpr uint64_t DefaultAttributeCacheLimit = 10 * 1024 * 1024;

// 4GB limit on individual file size 
constexpr uint64_t UPLOAD_FILESIZE_LIMIT = (uint64_t)4 * 1024 * 1024 * 1024;

// Linux has filename limit of 255, due to hex encoding we can only support source filenames of length 127
constexpr int UPLOAD_FILENAME_LIMIT = 127;
//---------------------------------------------------------------------------------------------------------------------