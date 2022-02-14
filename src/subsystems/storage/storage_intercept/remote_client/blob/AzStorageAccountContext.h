//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <storage_credential.h>
#include <storage_account.h>
//---------------------------------------------------------------------------------------------------------------------
struct AzStorageAccountContext
{
    AzStorageAccountContext() {};

    AzStorageAccountContext(
        /*[in]*/ const std::string& azStorageAccountName,
        /*[in]*/ const std::string& azStorageEndpointUrl)
        : accountName(azStorageAccountName)
        , endpointUrl(azStorageEndpointUrl) {}
    
    std::string accountName;
    std::string endpointUrl;
};
//---------------------------------------------------------------------------------------------------------------------