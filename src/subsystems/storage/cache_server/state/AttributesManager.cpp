///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "AttributesManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
AttributesManager::AttributesManager()
: m_vAttributesMap(NUM_MAPS),
  m_vAttributesMutex(NUM_MAPS),
  m_anAttributeCacheLimit(DefaultAttributeCacheLimit),
  m_anTotalCachedAttributes(0)
{
}
//---------------------------------------------------------------------------------------------------------------------
AttributesManager::~AttributesManager()
{
}
//---------------------------------------------------------------------------------------------------------------------
AttributesManager& AttributesManager::GetInstance()
{
    static AttributesManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void AttributesManager::SetAttributeCacheLimit(const uint64_t limitCacheEntries)
{
    m_anAttributeCacheLimit = limitCacheEntries;
}
//---------------------------------------------------------------------------------------------------------------------
bool AttributesManager::GetAndInitAttribute(const std::string& fileName, CacheAttributes& fileAttributes)
{
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vAttributesMutex[index]);
    auto iter = m_vAttributesMap[index].find(fileName);
    if (iter == m_vAttributesMap[index].end())
    {
        m_vAttributesMap[index].insert(make_pair(fileName, CacheAttributes()));
        return false;
    }
    else
    {
        fileAttributes = iter->second;
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool AttributesManager::GetAttribute(const std::string& fileName, CacheAttributes& fileAttributes)
{
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vAttributesMutex[index]);
    auto iter = m_vAttributesMap[index].find(fileName);
    if (iter == m_vAttributesMap[index].end())
    {
        return false;
    }
    else
    {
        fileAttributes = iter->second;
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool AttributesManager::PutAttributes(const cache::PutAttributeRequest& putAttributeRequest)
{
    if ((m_anTotalCachedAttributes.fetch_add(putAttributeRequest.fileattributes_size()) +
            putAttributeRequest.fileattributes_size()) > m_anAttributeCacheLimit)
    {
        LOG_WARN_REQID("TotalCachedAttributes: {}, exceeds limit: {}", putAttributeRequest, m_anTotalCachedAttributes, m_anAttributeCacheLimit);
        m_anTotalCachedAttributes.fetch_sub(putAttributeRequest.fileattributes_size());
        return false;
    }

    if (putAttributeRequest.fileattributes_size() == 1)
    {
        auto &fileAttr = putAttributeRequest.fileattributes(0);
        const string& fileName = fileAttr.filename();
        auto index = GetHashBucket(fileName);

        lock_guard lockGuard(m_vAttributesMutex[index]);
        (m_vAttributesMap[index])[fileName] = CacheAttributes(
            fileAttr.fileattribute().isdir(),
            fileAttr.fileattribute().filesize(),
            fileAttr.fileattribute().accessedtime(),
            fileAttr.fileattribute().modifiedtime());
        return true;
    }
    
    // Add multiple entries and optimize for locking
    vector<vector<const ::cache::FileAttributes*>> vecAttrEntries(NUM_MAPS);
    for (int iter = 0; iter < putAttributeRequest.fileattributes_size(); iter++)
    {
        const ::cache::FileAttributes* fileAttr = &putAttributeRequest.fileattributes(iter);
        auto index = GetHashBucket(fileAttr->filename());
        vecAttrEntries[index].push_back(fileAttr);
    }

    for (int index = 0; index < NUM_MAPS; index++)
    {
        if (vecAttrEntries[index].empty())
        {
            continue;
        }

        lock_guard lockGuard(m_vAttributesMutex[index]);
        for (auto fileAttr : vecAttrEntries[index])
        {
            (m_vAttributesMap[index])[fileAttr->filename()] = CacheAttributes(
                fileAttr->fileattribute().isdir(),
                fileAttr->fileattribute().filesize(),
                fileAttr->fileattribute().accessedtime(),
                fileAttr->fileattribute().modifiedtime());
        }
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------