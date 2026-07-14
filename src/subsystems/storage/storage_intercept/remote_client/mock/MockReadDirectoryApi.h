//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <vector>
#include "DirectoryEntry.h"
#include "RemoteClientApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
class MockReadDirectoryApi : public RemoteClientApiBase
{
    public:
        MockReadDirectoryApi(
            /*[in] */ const std::string& dirPath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries);

        void PerformOperation() override;

        std::string GetResult() const;

    private:
        // Inputs parameters
        const std::string m_strDirPath;
        const std::string m_strContToken;
        std::vector<DirectoryEntry>& m_vecDirEntries;

        // API result (new continuation token)
        std::string m_strNewContToken;
};
//---------------------------------------------------------------------------------------------------------------------