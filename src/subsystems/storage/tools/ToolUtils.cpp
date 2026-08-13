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

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>
#include <string_view>
#include <cstdint>
#include <cstring>

// Include existing project headers to reuse functionality
#include "CacheUtils.h"
#include "StorageInterceptConstants.h"
#include "CacheToolsLogger.h"

// Include our local header with function declarations
#include "ToolUtils.h"

using namespace std;
using namespace cacheutils;

//---------------------------------------------------------------------------------------------------------------------
// Implementation of function declarations from GetCacheKeyAndServerIndex.h
//---------------------------------------------------------------------------------------------------------------------

std::string GenerateCacheKey(const std::string& accountName, 
                            const std::string& containerName,
                            const std::string& blobPath, 
                            int64_t offset,
                            uint32_t chunkSize,
                            bool ignoreAccountName)
{
    std::string cachePrefix;
    std::string chunkSizeSuffix;
    
    // Handle account name in cache prefix based on ignoreAccountName flag
    if (ignoreAccountName)
    {
        cachePrefix = StorageInterceptConstants::CacheKeyAccountNameOverride; // Use constant from existing codebase
    }
    else
    {
        cachePrefix = accountName;
    }
    
    // Include chunk size in suffix only if it's not the default 4MB (4194304 bytes)
    constexpr uint32_t DEFAULT_CHUNK_SIZE_4_MIB = 4 * 1024 * 1024;
    if (chunkSize != DEFAULT_CHUNK_SIZE_4_MIB)
    {
        chunkSizeSuffix = ":" + std::to_string(chunkSize);
    }
    
    // Build the cache key: <cache_prefix>/<container_name>/<blob_path>:<offset>:<chunk_size>
    std::string cacheKeyInput = "/" + cachePrefix + "/" + containerName + "/" + blobPath + ":" + std::to_string(offset) + chunkSizeSuffix;
    
    LOG_INFO("Cache key input path: {}", cacheKeyInput);
    // Return SHA256 hash using existing utility function
    return GetSHA256Hex(cacheKeyInput);
}

//---------------------------------------------------------------------------------------------------------------------
bool ParseBool(const std::string& str)
{
    if (str == "true" || str == "1" || str == "yes")
        return true;
    if (str == "false" || str == "0" || str == "no")
        return false;
    
    throw std::invalid_argument("Invalid boolean value: " + str);
}

//---------------------------------------------------------------------------------------------------------------------