//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "TestBase.h"
#include "LocalAttributesCache.h"
#include <string>
#include <atomic>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class LocalAttributesCacheTest : public TestBase
{
    protected:
        LocalAttributesCacheTest();
        void SetUp() override;

        std::shared_ptr<IAttributesCache>       m_attributesCacheClient;
        static std::atomic_int                  m_fileCount;
        std::string                             m_strTestId;

        std::string GetRandFilename();
};
//---------------------------------------------------------------------------------------------------------------------