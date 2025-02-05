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
std::string GetSnapshotFolderPath();
void CleanupFolder(const std::string& folderPath);
void ResetCacheFolder();
void InitializeUtils();
unsigned int GetHashBucket(const std::string& strKey);
bool IsAuthEnabled();
std::string GetServerHostName();
std::string GetCacheServerMetricPort();
bool ValidateAuthSignature(const std::string& clientAccountName, const std::string& clientSignature,
    const std::string& message, time_t timestamp);
std::string GetUuid();
std::string FileNameEncode(const std::string& fileName);
bool GetCacheServersHelper(::google::protobuf::RepeatedPtrField<::std::string>& serveraddresses, const std::string& requestid);
//---------------------------------------------------------------------------------------------------------------------