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
#include "Utils.h"
#include "CacheToolsLogger.h"

// Include our local header with function declarations
#include "ToolUtils.h"

using namespace std;
using namespace cacheutils;


//---------------------------------------------------------------------------------------------------------------------
// Print usage information
//---------------------------------------------------------------------------------------------------------------------
void PrintUsage(const char* programName)
{
    std::string msg = "Invalid number of arguments.";
    LOG_ERROR("{}", msg);
    std::cerr << msg << std::endl;
    std::cerr << "Usage: " << programName << " <account_name> <container_name> <blob_path> <offset> <chunk_size> <ignore_account_name> <num_servers>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Parameters:" << std::endl;
    std::cerr << "  account_name         - Storage account name" << std::endl;
    std::cerr << "  container_name       - Container name" << std::endl;
    std::cerr << "  blob_path           - Path to the blob within the container" << std::endl;
    std::cerr << "  offset              - File offset (bytes)" << std::endl;
    std::cerr << "  chunk_size          - Chunk size (bytes)" << std::endl;
    std::cerr << "  ignore_account_name - Whether to ignore account name in cache key (true/false)" << std::endl;
    std::cerr << "  num_servers         - Number of cache servers" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Example:" << std::endl;
    std::cerr << "  " << programName << " myaccount mycontainer path/to/file.txt 0 4194304 false 4" << std::endl;
}


//---------------------------------------------------------------------------------------------------------------------
// Main function
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();

    if (argc != 8)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    
    try
    {
        // Parse command line arguments
        std::string accountName = argv[1];
        std::string containerName = argv[2];
        std::string blobPath = argv[3];
        int64_t offset = std::stoll(argv[4]);
        uint32_t chunkSize = std::stoul(argv[5]);
        bool ignoreAccountName = ParseBool(argv[6]);
        uint32_t numServers = std::stoul(argv[7]);
        
        // Validate inputs
        if (offset < 0)
        {
            std::string err = "Offset must be non-negative";
            LOG_ERROR("{}", err);
            std::cerr << err << std::endl;
            return 1;
        }
        
        if (chunkSize == 0)
        {
            std::string err = "Chunk size must be greater than 0";
            LOG_ERROR("{}", err);
            std::cerr << err << std::endl;
            return 1;
        }
        
        if (numServers == 0)
        {
            std::string err = "Number of servers must be greater than 0";
            LOG_ERROR("{}", err);
            std::cerr << err << std::endl;
            return 1;
        }
        
        // Generate cache key
        std::string cacheKey = GenerateCacheKey(accountName, containerName, blobPath, offset, chunkSize, ignoreAccountName);
        
        // Calculate server index using existing utility function
        uint32_t serverIndex = GetServerIndex(cacheKey.c_str(), numServers);

        uint32_t serverFolderHash = GetHashBucket(cacheKey);
        std::string serverFilePath = "/" + to_string(serverFolderHash) + "/" + FileNameEncode(cacheKey);

        // Output results
        // Output input and results as JSON to console
        std::ostringstream json;
        json << "{";
        json << "  \"AccountName\": \"" << accountName << "\",";
        json << "  \"ContainerName\": \"" << containerName << "\",";
        json << "  \"BlobPath\": \"" << blobPath << "\",";
        json << "  \"Offset\": " << offset << ",";
        json << "  \"ChunkSize\": " << chunkSize << ",";
        json << "  \"IgnoreAccountName\": " << (ignoreAccountName ? "true" : "false") << ",";
        json << "  \"NumberOfServers\": " << numServers << ",";
        json << "  \"CacheKey\": \"" << cacheKey << "\",";
        json << "  \"ServerIndex\": " << serverIndex << ",";
        json << "  \"ServerFilePath\": \"" << serverFilePath << "\"";
        json << "}";

        LOG_INFO("{}", json.str());
        std::cout << json.str();
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::string err = "Exception occurred: " + std::string(e.what());
        LOG_ERROR("{}", err);
        std::cerr << err << std::endl;
        return 1;
    }
}
//---------------------------------------------------------------------------------------------------------------------