//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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