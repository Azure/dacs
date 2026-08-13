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

#include "HashingFactory.h"
#include "StaticModuloHasher.h"
#include "ConsistentHasher.h"
#include "CommonLogger.h"
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<BaseHasher> HashingFactory::CreateHasher(HashingStrategy strategy) {
    switch(strategy)
    {
        case HashingStrategy::StaticModuloHashing:
        {
            LOG_INFO("Static modulo hashing enabled for server selection");
            return std::make_unique<StaticModuloHasher>();
        }
        case HashingStrategy::ConsistentHashing:
        {
            auto consistentHasher = std::make_unique<ConsistentHasher>();
            LOG_INFO("Consistent hashing enabled with {} virtual nodes per server", 
                consistentHasher->GetVirtualNodeCount());
            return consistentHasher;
        }
        default:
        {
            LOG_INFO("Defaulting to static modulo hashing");
            return std::make_unique<StaticModuloHasher>();
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
