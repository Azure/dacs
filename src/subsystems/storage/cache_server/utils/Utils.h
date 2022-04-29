///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "cache.pb.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
void SetCacheRootPath(const std::string& rootFolder);
std::string GetCacheFolderPath();
std::string GetCacheTempFolderPath();
std::string GetCacheTempPath(const std::string& fileName);
std::string GetCachePath(const std::string& fileName);
void Initialize();
unsigned int GetHashBucket(const std::string& strKey);
bool IsAuthEnabled();
bool ValidateAuthSignature(const std::string& clientAccountName, const std::string& clientSignature,
    const std::string& message, time_t timestamp);
//---------------------------------------------------------------------------------------------------------------------