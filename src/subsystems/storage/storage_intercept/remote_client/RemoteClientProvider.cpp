//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteClientProvider.h"
#include "StorageInterceptLogger.h"
#include "adls/AdlsClient.h"
#include "blob/BlobClient.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include "mock/MockRemoteClient.h"
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<IRemoteClient> RemoteClientProvider::GetRemoteClient()
{
    const std::string remoteStoreType = StorageInterceptConfig::GetInstance().GetRemoteStoreType();
    LOG_INFO("Initializing remote client of type {}", remoteStoreType);

    if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeBlob)
    {
        return std::make_shared<BlobClient>();
    }
    else if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeAdls)
    {
        return std::make_shared<AdlsClient>();
    }
    else if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeMock)
    {
        return std::make_shared<MockRemoteClient>();
    }

    throw "Unsupported remote store type in config";
    return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------