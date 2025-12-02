//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include <string>
#include "FileData.h"
//---------------------------------------------------------------------------------------------------------------------
class TestBase : public ::testing::Test
{
    public:
        TestBase();

        void TearDown() override;
        void OverrideConfig(const char* configName, const std::string& configValue);

        static void SetupMockRemoteClient();
        static std::string GenerateRandomString(uint16_t length);
        static void GenerateRandomValueBuf();
        static std::shared_ptr<FileData> GetRandDataBuffer(uint64_t size);
        static bool FileDataEquals(
            /*[in]*/ const std::shared_ptr<FileData>& spFirst,
            /*[in]*/ const std::shared_ptr<FileData>& spSecond);

        // Is using mock
        static bool m_bUsingMock;

        // Intercept path
        inline static const std::string m_strInterceptPath = "/mnt/dataPathIntcpt";

        // Remote path for a test file which we can read
        inline static const std::string m_strRemoteFilePath = "testFileForUt";
        static constexpr uint32_t REMOTE_FILE_SIZE = 16 * 1024 * 1024; // 16MB
        static constexpr uint32_t DEFAULT_STREAMING_CHUNK_SIZE = 4 * 1024 * 1024; // 4MB
        static std::vector<char> m_vecFileBuf;

        // Remote path for a test dir and a file inside it
        inline static const std::string m_strRemoteDirPath = "testDirForUt";
        inline static const std::string m_strRemoteDirChildDirPath = "testDirForUt/dir-1";
        inline static const std::string m_strRemoteDirChildFilePath = "testDirForUt/file-1";
    
    protected:
        bool m_bConfigUpdated;
};
//---------------------------------------------------------------------------------------------------------------------
