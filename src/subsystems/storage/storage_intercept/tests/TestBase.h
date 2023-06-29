//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
class TestBase : public ::testing::Test
{
    public:
        TestBase();

        void TearDown() override;
        void OverrideConfig(const char* configName, const std::string& configValue);

        static void SetupMockRemoteClient();
        static void GenerateRandomValueBuf();

        // Is using mock
        static bool m_bUsingMock;

        // Intercept path
        inline static const std::string m_strInterceptPath = "/mnt/dataPathIntcpt";

        // Remote path for a test file which we can read
        inline static const std::string m_strRemoteFilePath = "testFileForUt";
        static constexpr uint32_t REMOTE_FILE_SIZE = 16 * 1024 * 1024;
        static std::vector<char> m_vecFileBuf;

        // Remote path for a test dir and a file inside it
        inline static const std::string m_strRemoteDirPath = "testDirForUt";
        inline static const std::string m_strRemoteDirChildDirPath = "testDirForUt/dir-1";
        inline static const std::string m_strRemoteDirChildFilePath = "testDirForUt/file-1";
    
    protected:
        bool m_bConfigUpdated;
};
//---------------------------------------------------------------------------------------------------------------------
