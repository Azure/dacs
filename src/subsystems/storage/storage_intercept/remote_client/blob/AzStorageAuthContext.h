//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "AzStorageAuthType.h"
#include <string>
#include <memory>
#include <storage_credential.h>
#include <storage_account.h>
//---------------------------------------------------------------------------------------------------------------------
struct AzStorageAuthContext
{
    typedef azure::storage_lite::storage_credential                     storageCredential_t;
    typedef azure::storage_lite::shared_key_credential                  sharedKeyCredential_t;
    typedef azure::storage_lite::shared_access_signature_credential     sasCredential_t;
    typedef azure::storage_lite::token_credential                       tokenCredential_t;

    AzStorageAuthContext() {}

    AzStorageAuthContext(
        /*[in]*/ const std::string& azStorageAccountName,
        /*[in]*/ const std::string& azStorageAccountKey)
    {
        authType = AzStorageAuthType::StorageAccountKey;
        credential = std::make_shared<sharedKeyCredential_t>(azStorageAccountName, azStorageAccountKey);
    }

    AzStorageAuthContext(
        /*[in]*/ AzStorageAuthType tokenType,
        /*[in]*/ const std::string& token)
    {
        authType = tokenType;
        if (tokenType == AzStorageAuthType::SASToken)
        {
            credential = std::make_shared<sasCredential_t>(token);
        }
        else if (tokenType == AzStorageAuthType::AccessToken)
        {
            credential = std::make_shared<tokenCredential_t>(token);
        }
        else
        {
            throw "Unsupported token type passed";
        }
    }

    AzStorageAuthType authType;
    std::shared_ptr<storageCredential_t> credential;
};
//---------------------------------------------------------------------------------------------------------------------