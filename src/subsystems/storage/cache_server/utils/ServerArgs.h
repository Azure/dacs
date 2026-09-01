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
//
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Constants.h"
#include <cstdint>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
// Named CacheServer startup arguments, parsed via boost::program_options (see ParseServerArgs).
struct ServerArgs
{
    int port = -1;
    std::string cacheRootPath = ".";
    uint64_t cacheSizeLimitBytes = DefaultCacheSizeLimitBytes;
    uint32_t cacheTTLWindowSeconds = DefaultCacheTTLWindowSizeSeconds;
};
//---------------------------------------------------------------------------------------------------------------------
enum class ParseServerArgsResult
{
    Success,
    HelpRequested,
    Error
};
//---------------------------------------------------------------------------------------------------------------------
// Parses named CacheServer command line arguments (e.g. --port 9065 --sizeLimitBytes 19903423432).
// On HelpRequested or Error, outUsageMessage is populated with usage/help text or an error description
// respectively and the caller should print it and exit; outArgs is only valid when Success is returned.
ParseServerArgsResult ParseServerArgs(int argc, char* argv[], ServerArgs& outArgs, std::string& outUsageMessage);
//---------------------------------------------------------------------------------------------------------------------
