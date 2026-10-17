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

#pragma once
#include <gtest/gtest.h>
#include <memory>

#include "CacheClientImpl.h"
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTests : public ::testing::TestWithParam<bool>
{
  protected:
    void SetUp() override;
    void TearDown() override;
    const char* GetRandomBuffer(const uint64_t nLength) const;
    void SetCacheServerCount(int count) const;
    static void InitializeClient(
      CacheClientImpl& client,
      bool bIsDiscoveryEnabled = false,
      bool bEnableCalculateChecksum = false,
      bool bEnableValidateChecksum = false,
      const char* strChecksumAlgorithm = nullptr,
      bool bEnableConsistentHashing = false,
      ChecksumMetadataPolicy checksumMetadataPolicy = CHECKSUM_METADATA_OPTIONAL);

    // Discovery test helper methods
    bool WaitForServerCount(int expectedCount, int timeoutSeconds = 10) const;
    std::vector<std::string> GetRingHostnames() const;
    bool IsDiscoveryTest() const { return GetParam(); }
    void AssertServersContain(const std::vector<std::string>& servers, const std::string& expectedServer) const;

    // Test constants
    static constexpr const char* UNREACHABLE_ENDPOINT = "192.0.2.1";  // RFC5737 test address
    // Timeout must comfortably exceed the discovery interval to avoid races.
    static constexpr int DEFAULT_DISCOVERY_TIMEOUT = 8;
    static constexpr int EXTENDED_DISCOVERY_TIMEOUT = 15;

    CacheClientImpl m_cacheClientImpl;
    CacheClientImpl m_cacheClientImplCRC;
    CacheClientImpl m_cacheClientImplCRCMetadataRequired;
};
//---------------------------------------------------------------------------------------------------------------------