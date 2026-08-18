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

#include "RemoteClientProvider.h"
#include "StorageInterceptLogger.h"
#include "adls/AdlsClient.h"
#include "blob/BlobClient.h"
#include "blob/SdkBlobClient.h"
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
        // Use the azure-sdk-for-cpp client when remoteBlobUseSdkClient is set; else legacy cpplite.
        if (StorageInterceptConfig::GetInstance().GetRemoteBlobUseSdkClient(mountName))
        {
            LOG_INFO("Using SdkBlobClient (azure-sdk-for-cpp) for mountName={}", mountName);
            return std::make_shared<SdkBlobClient>(mountName);
        }
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