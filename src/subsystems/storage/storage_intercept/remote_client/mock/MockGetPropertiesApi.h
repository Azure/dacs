//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteClientApiBase.h"
#include "FileAttributes.h"
//---------------------------------------------------------------------------------------------------------------------
class MockGetPropertiesApi : public RemoteClientApiBase
{
    public:
        MockGetPropertiesApi(
            /*[in] */ const std::string& filePath,
            /*[out]*/ FileAttributes& attributes);

        void PerformOperation() override;

        int GetResult() const;

    private:
        // Inputs parameters
        const std::string m_strFilePath;
        FileAttributes& m_attributes;

        // API result
        int m_nResult;
};
//---------------------------------------------------------------------------------------------------------------------