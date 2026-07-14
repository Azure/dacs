//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
