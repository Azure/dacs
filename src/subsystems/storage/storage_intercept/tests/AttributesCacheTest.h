//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "TestBase.h"
#include "LocalAttributesCache.h"
#include <string>
#include <atomic>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class AttributesCacheTest : public TestBase
{
    protected:
        AttributesCacheTest();
        void SetUp() override;

        std::shared_ptr<IAttributesCache>       m_attributesCacheClient;
        static std::atomic_int                  m_fileCount;
        std::string                             m_strTestId;

        std::string GetRandFilename();
};
//---------------------------------------------------------------------------------------------------------------------