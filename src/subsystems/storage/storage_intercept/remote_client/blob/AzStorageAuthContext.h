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