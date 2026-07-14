//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteClientProvider.h"
#include "StorageInterceptLogger.h"
#include "adls/AdlsClient.h"
#include "blob/BlobClient.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include "mock/MockRemoteClient.h"
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<IRemoteClient> RemoteClientProvider::GetRemoteClient(const std::string& mountName)
{
    const std::string remoteStoreType = StorageInterceptConfig::GetInstance().GetRemoteStoreType(mountName);
    LOG_INFO("Initializing remote client of name={} type={}", mountName, remoteStoreType);

    if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeBlob)
    {
        return std::make_shared<BlobClient>(mountName);
    }
    else if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeAdls)
    {
        return std::make_shared<AdlsClient>(mountName);
    }
    else if (remoteStoreType == StorageInterceptConstants::ConfigValRemoteTypeMock)
    {
        return std::make_shared<MockRemoteClient>(mountName);
    }

    throw "Unsupported remote store type in config";
    return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------