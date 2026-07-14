//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
enum class AzStorageAuthType
{
    // General SAS token type, could be: User delegation SAS or Service SAS or Account SAS
    SASToken = 1,

    // An accessToken obtained from managed identity
    AccessToken = 2,

    StorageAccountKey = 3
};
//---------------------------------------------------------------------------------------------------------------------