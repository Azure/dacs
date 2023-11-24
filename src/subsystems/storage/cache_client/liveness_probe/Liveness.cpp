///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
///--------------------------------------------------------------------------------------------------------------------
// Liveness probe - this program is used by K8s to check if the cache server is alive and responding to requests.
///--------------------------------------------------------------------------------------------------------------------
#include <stdlib.h>
#include <string>

#include "CacheClient.h"
#include "CacheClientImpl.h"
#include "CacheUtils.h"
#include "LoggingApi.h"
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
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
            nCacheServerPort = std::atoi(envPort);
        }
    }

    LOG_INFO("Starting liveness probe. Connecting to cache server at {}:{}", strCacheServerHostName, nCacheServerPort);

    CacheClientImpl cacheClient;
    const char* arrServers[1] = {strCacheServerHostName.c_str()};
    cacheClient.Initialize(arrServers,
                           1, /*nServers*/
                           nCacheServerPort,
                           false,   /*bEnableIB*/
                           false,   /*bEnableCalculateChecksum*/
                           false,   /*bEnableValidateChecksum*/
                           0,       /*nConnectionPoolSize*/
                           false,   /*bEnableAuth*/
                           nullptr, /*strAccountName*/
                           nullptr, /*strAccountKey*/
                           false,   /*bUseLocalServer*/
                           true,    /*bReuseConnections*/
                           0 /*nMaxBufferSizeBytes*/);

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
        LOG_ERROR("Liveness probe failed. Download status: {}", status);
        return 1;
    }
}
//---------------------------------------------------------------------------------------------------------------------
