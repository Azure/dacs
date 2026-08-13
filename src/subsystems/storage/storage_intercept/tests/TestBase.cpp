// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include "TestBase.h"
#include <iostream>
#include <map>
#include <mutex>
#include <fstream>
#include <sys/xattr.h>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <execinfo.h>
//---------------------------------------------------------------------------------------------------------------------
bool TestBase::m_bUsingMock;
std::vector<char> TestBase::m_vecFileBuf;
//---------------------------------------------------------------------------------------------------------------------
void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    // Print backtrace
    void* array[10];
    size_t size = backtrace(array, 10);
    char** messages = backtrace_symbols(array, size);

    std::cerr << "Stack trace:" << std::endl;
    for (size_t i = 0; i < size; i++) {
        std::cerr << messages[i] << std::endl;
    }

    free(messages);

    // Terminate program
    exit(signum);
}

TestBase::TestBase()
    : m_bConfigUpdated(false)
{
    static std::once_flag randomSeedFlag;
    std::call_once(randomSeedFlag, [](){srand(time(NULL));});

    static std::once_flag sigHandlerFlag;
    std::call_once(sigHandlerFlag, []() {
        std::cout << "Enabling Sig Handler for SIGSEGV" << std::endl;
        // Register signal handler for SIGSEGV to prevent process from crashing
        if (::signal(SIGSEGV, signalHandler) == SIG_ERR) {
            std::cout << "Registering SIGSEGV signal handler failed" << std::endl;
        }
    });

    const char* envUseEXistingConfig = std::getenv("STORAGE_INTERCEPT_USE_EXISTING_CONFIG");
    if (envUseEXistingConfig && std::string(envUseEXistingConfig).compare("true") == 0)
    {
        m_bUsingMock = false;
        std::cout << "Using pre-existing config file for tests" << std::endl;
        return;
    }
    m_bUsingMock = true;
    static std::once_flag mockSetupFlag;
    std::call_once(mockSetupFlag, [this](){this->SetupMockRemoteClient();});
}
//---------------------------------------------------------------------------------------------------------------------
void TestBase::TearDown()
{
    if (m_bConfigUpdated)
    {
        StorageInterceptConfig::GetInstance().ReloadConfig();
        m_bConfigUpdated = false;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void TestBase::OverrideConfig(const char* configName, const std::string& configValue)
{
    StorageInterceptConfig::GetInstance().OverrideConfig(configName, configValue);
    m_bConfigUpdated = true;
}
//---------------------------------------------------------------------------------------------------------------------
/* static */ std::string TestBase::GenerateRandomString(uint16_t length)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "0123456789";
    const size_t maxIndex = sizeof(charset) - 1;
    std::string randomString;
    randomString.reserve(length);
    for (uint16_t i = 0; i < length; ++i) {
        randomString += charset[rand() % maxIndex];
    }
    return randomString;
}
//---------------------------------------------------------------------------------------------------------------------
void TestBase::GenerateRandomValueBuf()
{
    m_vecFileBuf.clear();
    m_vecFileBuf.assign(REMOTE_FILE_SIZE, 'A');
    size_t nMutations = std::min(m_vecFileBuf.size(), static_cast<size_t>(1000000));
    for(size_t idx = 0; idx < nMutations; idx++)
    {
        size_t randomIdx = rand() % m_vecFileBuf.size();
        char randomValue = 'A' + rand() % 26;
        m_vecFileBuf[randomIdx] = randomValue;
    }
}
//---------------------------------------------------------------------------------------------------------------------
/* static */ bool TestBase::FileDataEquals(
    const std::shared_ptr<FileData>& spFirst,
    const std::shared_ptr<FileData>& spSecond)
{
    if (spFirst->BufferSize != spSecond->BufferSize)
    {
        return false;
    }

    bool bResult = true;
    const size_t nSize = spFirst->BufferSize;
    const char* pFirstData = static_cast<const char*>(spFirst->Buffer.get());
    const char* pSecondData = static_cast<const char*>(spSecond->Buffer.get());

    for (size_t idx = 0; idx < nSize; idx++)
    {
        if (pFirstData[idx] != pSecondData[idx])
        {
            bResult = false;
            break;
        }
    }
    return bResult;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<FileData> TestBase::GetRandDataBuffer(uint64_t size)
{
    std::shared_ptr<FileData> fileData = std::make_shared<FileData>(
        0,
        size,
        nullptr /* userBuf */,
        false /* setToZeros */);
    std::generate(fileData->Buffer.get(), fileData->Buffer.get() + size, []{return rand() % 255;});
    return fileData;
}
//---------------------------------------------------------------------------------------------------------------------
void TestBase::SetupMockRemoteClient()
{
    std::string mockClientRootDir = "/tmp/mock_remote_client_test_" + std::to_string( (rand() % 10000) * (rand() % 10000) );
    std::string mockClientTestFilePath = mockClientRootDir + "/" + m_strRemoteFilePath;
    std::string mockClientTestDirPath = mockClientRootDir + "/" + m_strRemoteDirPath;
    std::string mockClientTestDirChildDirPath = mockClientRootDir + "/" + m_strRemoteDirChildDirPath;
    std::string mockClientTestDirChildFilePath = mockClientRootDir + "/" + m_strRemoteDirChildFilePath;

    // Step 1: Create mock remote clients root directory and test file and test dir
    // Create remote directory
    ::mkdir(mockClientRootDir.c_str(), 0777);
    std::cout << "MockRemoteClient Setup created root dir " << mockClientRootDir << std::endl;

    // Create and Write to test file
    int nFd = ::open(mockClientTestFilePath.c_str(), O_RDWR | O_CREAT, 0777);
    ASSERT_NE(nFd, -1);

    // Generate test file contents with random values in buffer
    GenerateRandomValueBuf();
    int nBytesWritten = ::write(nFd, m_vecFileBuf.data(), REMOTE_FILE_SIZE);
    ASSERT_EQ(nBytesWritten, REMOTE_FILE_SIZE);

    ASSERT_EQ(::close(nFd), 0);
    std::cout << "MockRemoteClient Setup created test file " << mockClientTestFilePath << std::endl;

    // Set dacsfileversion xattr on test file for version metadata testing
    std::string xattrName = std::string("user.") + StorageInterceptConstants::BlobMetadataKeyVersion;
    std::string xattrValue = "mock-test-version-001";
    setxattr(mockClientTestFilePath.c_str(), xattrName.c_str(), xattrValue.c_str(), xattrValue.size(), 0);

    // Create test dir and it's children
    ::mkdir(mockClientTestDirPath.c_str(), 0777);
    ::mkdir(mockClientTestDirChildDirPath.c_str(), 0777);
    nFd = ::open(mockClientTestDirChildFilePath.c_str(), O_RDWR | O_CREAT, 0777);
    ASSERT_EQ(::close(nFd), 0);

    // Set dacsfileversion xattr on child file for version metadata testing
    setxattr(mockClientTestDirChildFilePath.c_str(), xattrName.c_str(), xattrValue.c_str(), xattrValue.size(), 0);

    std::cout << "MockRemoteClient Setup created test dir " << mockClientTestDirPath << std::endl;

    std::map<std::string, std::string> mockClientConfig;
    mockClientConfig[StorageInterceptConstants::ConfigNameMockClientRoot] = mockClientRootDir;

    // Step 2: Write the config
    std::ofstream outConfig(StorageInterceptConstants::DefaultConfigFilePath);
    outConfig
        << StorageInterceptConstants::ConfigNameStoragePath
        << ' ' << m_strInterceptPath << std::endl
        << StorageInterceptConstants::ConfigNameRemoteType
        << ' ' << StorageInterceptConstants::ConfigValRemoteTypeMock << std::endl
        << StorageInterceptConstants::ConfigNameMockClientRoot
        << ' ' << mockClientRootDir << std::endl
        << StorageInterceptConstants::ConfigNameCacheEnable
        << " false" << std::endl
        << StorageInterceptConstants::ConfigNameAttributesCacheEnable
        << " false" << std::endl;

    outConfig.close();

    std::cout << "MockRemoteClient Setup completed." << std::endl;
}
//---------------------------------------------------------------------------------------------------------------------
