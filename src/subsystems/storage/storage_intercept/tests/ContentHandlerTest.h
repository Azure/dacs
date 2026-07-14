//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "TestBase.h"
#include <vector>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
class ContentHandlerTest : public TestBase
{
    public:
        struct OpenReadAndCloseTask
        {
            std::string     filePath;
            int             openFlags;
            size_t          readSize;
        };

        void OpenReadAndCloseCheck(/*[in]*/ OpenReadAndCloseTask task);
        void OpenAtReadAndCloseCheck(/*[in]*/ OpenReadAndCloseTask task);
        void FOpenReadAndCloseCheck(/*[in]*/ OpenReadAndCloseTask task);

        struct ReaddirTask
        {
            std::string dirPath;
            size_t      numFiles;
            size_t      numDirs;
        };

        void ReaddirCheck(/*[in]*/ ReaddirTask task);

        struct GetPropertiesTask
        {
            std::string     filePath;
            size_t          fileSize;
            std::string     dirPath;
        };

        void GetPropertiesCheck(/*[in]*/ GetPropertiesTask task);

    protected:
        ContentHandlerTest();

        void SetUp() override;
        void TearDown() override;

        template <typename task_t>
        void ExecuteParallelWithArgs(
            /*[in]*/ std::function<void(task_t)> function,
            /*[in]*/ std::vector<task_t> tasks,
            /*[in]*/ int parallelism);

        std::string m_strTestCaseDir;
};
//---------------------------------------------------------------------------------------------------------------------