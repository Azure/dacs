///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <vector>
#include "CacheUtils.h"
#include "FileAttributes.h"
#include "DirectoryEntry.h"
//---------------------------------------------------------------------------------------------------------------------
/*
 * Cache to store the file attributes fetched from remote store.
 */
class IAttributesCache
{
    public:
        virtual cacheutils::CacheStatus Get(
            /*[in] */ const std::string& path,
            /*[out]*/ FileAttributes& fileAttributes) = 0;

        virtual void Put(
            /*[in] */ const std::string& path,
            /*[in] */ const FileAttributes& fileAttributes) = 0;

        virtual void PutAll(
            /*[in] */ const std::string& pathPrefix,
            /*[in] */ const std::vector<DirectoryEntry>& dirEntries,
            /*[in] */ const size_t startIndex) = 0;

        virtual ~IAttributesCache() {};
};
//---------------------------------------------------------------------------------------------------------------------
