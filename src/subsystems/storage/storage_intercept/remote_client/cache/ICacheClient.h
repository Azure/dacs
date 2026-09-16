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
#include "FileData.h"
#include "CacheUtils.h"
#include <string>
#include <unistd.h>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class ICacheClient
{
    public:
        virtual cacheutils::CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char *buffer,
            /*[out]*/ uint64_t& readLength) = 0;

        virtual bool Upload(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ std::shared_ptr<const FileData> fileData,
            /*[in] */ bool isWrite) = 0;

        virtual bool DeleteGroup(const std::string&) { return true; }

        virtual ~ICacheClient() {};
};
//---------------------------------------------------------------------------------------------------------------------