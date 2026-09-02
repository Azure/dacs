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
#include "ServerLocatorCache.h"
#include "FlatMapIndex.h"
#include "FlatRadixIndex.h"
#include "LoggingApi.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

static std::unique_ptr<LocatorCache> s_spServerLocatorCache = std::make_unique<LocatorCache>();

LocatorCache& GetServerLocatorCache()
{
    return *s_spServerLocatorCache;
}

void InitServerLocatorCache(uint32_t nNodeId, uint32_t nNumNodes)
{
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

void InitServerLocatorCache(const std::string& strIndexBackend,
                            uint32_t nNodeId, uint32_t nNumNodes)
{
    std::string strNormalized = strIndexBackend;
    std::transform(strNormalized.begin(), strNormalized.end(), strNormalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::unique_ptr<ILocatorIndex> pIndex;
    if (strNormalized == "flatradix")
    {
        LOG_INFO("ServerLocatorCache: using FlatRadixIndex backend");
        pIndex = std::make_unique<FlatRadixIndex>();
    }
    else
    {
        if (!strNormalized.empty() && strNormalized != "flatmap")
        {
            LOG_WARN("ServerLocatorCache: unknown locator index backend '{}', falling back to FlatMapIndex",
                     strIndexBackend);
        }
        else
        {
            LOG_INFO("ServerLocatorCache: using FlatMapIndex backend");
        }
        pIndex = std::make_unique<FlatMapIndex>();
    }

    s_spServerLocatorCache = std::make_unique<LocatorCache>(std::move(pIndex));
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

void ResetServerLocatorCacheForTest(std::unique_ptr<ILocatorIndex> pIndex,
                                    uint32_t nNodeId, uint32_t nNumNodes)
{
    s_spServerLocatorCache = std::make_unique<LocatorCache>(std::move(pIndex));
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

} // namespace kvclient
