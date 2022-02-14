///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClientFactory.h"
#include "RemoteCacheClient.h"
#include "LocalFSCacheClient.h"
#include "StorageInterceptConfig.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<ICacheClient> CacheClientFactory::GetCacheClient()
{
    if (StorageInterceptConfig::GetInstance().GetCacheUseLocalFS())
    {
        // Should be used only for test and not in production as this is not a robust implementation
        return make_shared<LocalFSCacheClient>();
    }
    else
    {
        return make_shared<RemoteCacheClient>();
    }
}
//---------------------------------------------------------------------------------------------------------------------
