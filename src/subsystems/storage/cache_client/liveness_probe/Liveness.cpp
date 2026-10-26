// Copyright (c) Microsoft Corporation.
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

// Liveness probe - this program is used by K8s to check if the cache server is alive and responding to requests.
///--------------------------------------------------------------------------------------------------------------------
#include <stdlib.h>
#include <string>

#include "CacheClient.h"
#include "CacheClientImpl.h"
#include "CacheUtils.h"
#include "LoggingApi.h"
//---------------------------------------------------------------------------------------------------------------------
int main()
{
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();

    uint32_t nCacheServerPort = 9065;
    std::string strCacheServerHostName = "127.0.0.1";

    {
        char* envHost = std::getenv("TEST_CACHESERVER_ADDR_HOST");
        if(envHost != nullptr)
        {
            strCacheServerHostName = std::string(envHost);
        }

        char* envPort = std::getenv("TEST_CACHESERVER_ADDR_PORT");
        if(envPort != nullptr)
        {
            nCacheServerPort = static_cast<uint32_t>(std::stoul(envPort));
        }
    }

    LOG_INFO("Starting liveness probe. Connecting to cache server at {}:{}", strCacheServerHostName, nCacheServerPort);

    CacheClientImpl cacheClient;
    const char* arrServers[1] = {strCacheServerHostName.c_str()};
    const CacheClientRetryConfig retryConfig{
        1,
        0,
        0,
    };
    cacheClient.Initialize(arrServers,
                           1,       /*nServers*/
                           nCacheServerPort,
                           false,   /*bEnableIB*/
                           false,   /*bEnableCalculateChecksum*/
                           false,   /*bEnableValidateChecksum*/
                           nullptr, /*strChecksumAlgorithm*/
                           0,       /*nConnectionPoolSize*/
                           false,   /*bUseLocalServer*/
                           true,    /*bReuseConnections*/
                           false,   /*bIsCacheServerDiscoveryEnabled*/
                           300,     /*nCacheServerDiscoveryIntervalInSeconds*/
                           strCacheServerHostName.c_str(), /*nCacheServerDiscoveryEndpoint*/
                           false,   /*bEnableConsistentHashing*/
                           retryConfig,
                           CHECKSUM_METADATA_OPTIONAL,
                           0        /*nMaxBufferSizeBytes*/);

    std::string strFileName = "liveness_probe_" + cacheutils::GetCorrelationId();
    void* pBuffer = malloc(MAX_BUFFER_SIZE_BYTES);
    std::size_t nReadLength = MAX_BUFFER_SIZE_BYTES;

    CacheClientStatus status = cacheClient.Download(strFileName.c_str(),
                                                    pBuffer,
                                                    &nReadLength,
                                                    nullptr /*pMetadata*/,
                                                    nullptr /*strEncryptionKey*/,
                                                    0 /*nStartOffset*/,
                                                    false /*bEnableLock*/,
                                                    nullptr /*strCorrelationId*/);

    free(pBuffer);
    pBuffer = nullptr;

    if(status == CacheClientStatus::NOTFOUND)
    {
        LOG_INFO("Liveness probe succeeded");
        return 0;
    }
    else
    {
        LOG_ERROR("Liveness probe failed. Download status: {}", static_cast<int>(status));
        return 1;
    }
}
//---------------------------------------------------------------------------------------------------------------------
