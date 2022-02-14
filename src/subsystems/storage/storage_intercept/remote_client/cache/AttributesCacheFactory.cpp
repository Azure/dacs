///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "AttributesCacheFactory.h"
#include "LocalAttributesCache.h"
#include "InMemoryAttributesCache.h"
#include "StorageInterceptConfig.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<IAttributesCache> AttributesCacheFactory::GetAttributesCache()
{
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
    {
        return make_shared<LocalAttributesCache>();
    }
    else
    {
        return make_shared<InMemoryAttributesCache>();
    }
}
//---------------------------------------------------------------------------------------------------------------------
