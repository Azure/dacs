///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "LockManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
LockManager::LockManager()
: m_vLockedFilesSet(NUM_MAPS),
  m_vLockedFilesSetMutex(NUM_MAPS)
{
}
//---------------------------------------------------------------------------------------------------------------------
LockManager::~LockManager()
{
}
//---------------------------------------------------------------------------------------------------------------------
LockManager& LockManager::GetInstance()
{
    static LockManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
bool LockManager::GetLockForUpload(const std::string& fileName)
{
    auto index = GetHashBucket(fileName);

    bool ownFile = false;
    lock_guard lockGuard(m_vLockedFilesSetMutex[index]);
    if (m_vLockedFilesSet[index].find(fileName) == m_vLockedFilesSet[index].end())
    {
        m_vLockedFilesSet[index].insert(fileName);
        ownFile = true;
    }
    return ownFile;
}
//---------------------------------------------------------------------------------------------------------------------