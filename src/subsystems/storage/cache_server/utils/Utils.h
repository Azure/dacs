/// Copyright (c) Microsoft Corporation.
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
std::string GetServerHostName();
std::string GetCacheServerMetricPort();
std::string GetUuid();
std::string FileNameEncode(const std::string& fileName);
bool GetCacheServersHelper(::google::protobuf::RepeatedPtrField<::std::string>& serveraddresses, const std::string& requestid);
//---------------------------------------------------------------------------------------------------------------------