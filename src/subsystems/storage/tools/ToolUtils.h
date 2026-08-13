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

#include <string>
#include <cstdint>

//---------------------------------------------------------------------------------------------------------------------
// Function declarations for cache key and server index generation
// These functions are defined in GetCacheKeyAndServerIndex.cpp
//---------------------------------------------------------------------------------------------------------------------

/**
 * Generate a cache key similar to RemoteCacheClient::GetCacheKey
 * @param accountName Storage account name
 * @param containerName Container name
 * @param blobPath Path to the blob within the container
 * @param offset File offset in bytes
 * @param chunkSize Chunk size in bytes
 * @param ignoreAccountName Whether to ignore account name in cache key
 * @return SHA256 hash of the cache key input
 */
std::string GenerateCacheKey(const std::string& accountName, 
                            const std::string& containerName,
                            const std::string& blobPath, 
                            int64_t offset,
                            uint32_t chunkSize,
                            bool ignoreAccountName);

/**
 * Helper function to parse boolean from string
 * @param str String to parse ("true", "false", "1", "0", "yes", "no")
 * @return Boolean value
 * @throws std::invalid_argument if string cannot be parsed
 */
bool ParseBool(const std::string& str);

//---------------------------------------------------------------------------------------------------------------------