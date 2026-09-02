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
#include "LocatorCache.h"

#include <memory>
#include <string>
//---------------------------------------------------------------------------------------------------------------------

// Singleton accessor for the server-side locator cache instance.
// Initialised once at server startup via InitServerLocatorCache().
//---------------------------------------------------------------------------------------------------------------------
namespace kvclient
{
    LocatorCache& GetServerLocatorCache();

    // Initialise the server locator cache with the default backend (FlatMapIndex).
    void InitServerLocatorCache(uint32_t nNodeId, uint32_t nNumNodes);

    // Initialise the server locator cache, selecting the index backend by name.
    // Accepted values (case-insensitive): "flatmap" (default) and "flatradix".
    // Unrecognised names fall back to the default backend with a warning.
    void InitServerLocatorCache(const std::string& strIndexBackend,
                                uint32_t nNodeId, uint32_t nNumNodes);

    // Test seam: replace the server locator cache with one backed by the supplied index
    // (e.g. a FlatRadixIndex) and initialise it. Intended for unit tests only; production
    // code paths use InitServerLocatorCache with the default backend.
    void ResetServerLocatorCacheForTest(std::unique_ptr<ILocatorIndex> pIndex,
                                        uint32_t nNodeId, uint32_t nNumNodes);
}
//---------------------------------------------------------------------------------------------------------------------
