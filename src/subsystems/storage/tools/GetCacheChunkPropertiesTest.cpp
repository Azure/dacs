/// Copyright (c) Microsoft Corporation.
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

#include <gtest/gtest.h>
#include <string>
#include <cstdint>

// Include the same headers as the main tool
#include "CacheUtils.h"
#include "StorageInterceptConstants.h"

// Include our tool's header to test the actual implementation
#include "ToolUtils.h"

using namespace std;
using namespace cacheutils;

//---------------------------------------------------------------------------------------------------------------------
// Test fixture for GetCacheChunkProperties functionality
//---------------------------------------------------------------------------------------------------------------------
class GetCacheChunkPropertiesTest : public ::testing::Test 
{
protected:
    void SetUp() override 
    {
        // Common test data
        accountName = "myaccount";
        containerName = "mycontainer"; 
        blobPath = "path/to/file.txt";
        defaultChunkSize = 4 * 1024 * 1024; // 4MB
        customChunkSize = 8 * 1024 * 1024;  // 8MB
    }

    std::string accountName;
    std::string containerName;
    std::string blobPath;
    uint32_t defaultChunkSize;
    uint32_t customChunkSize;
};

//---------------------------------------------------------------------------------------------------------------------
// Test GetSHA256Hex function from existing codebase
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestSHA256Hex)
{
    // Test with known input
    std::string input = "test";
    std::string result = GetSHA256Hex(input);
    
    // SHA256 hash should be 64 characters (256 bits = 32 bytes = 64 hex chars)
    EXPECT_EQ(result.length(), 64U);
    
    // Should be consistent - same input should produce same output
    std::string result2 = GetSHA256Hex(input);
    EXPECT_EQ(result, result2);
    
    // Different input should produce different output
    std::string result3 = GetSHA256Hex("different");
    EXPECT_NE(result, result3);
}

//---------------------------------------------------------------------------------------------------------------------
// Test GetServerIndex function from existing codebase
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestGetServerIndex)
{
    const char* filePath = "test/file/path";
    
    // Test with single server
    EXPECT_EQ(GetServerIndex(filePath, 1), 0U);
    
    // Test with multiple servers - should return consistent result
    uint32_t numServers = 4;
    uint32_t serverIndex = GetServerIndex(filePath, numServers);
    EXPECT_LT(serverIndex, numServers);
    
    // Should be consistent for same input
    EXPECT_EQ(GetServerIndex(filePath, numServers), serverIndex);
    
    // Different paths might produce different server indices
    const char* differentPath = "different/file/path";
    uint32_t differentIndex = GetServerIndex(differentPath, numServers);
    EXPECT_LT(differentIndex, numServers);
}

//---------------------------------------------------------------------------------------------------------------------
// Test cache key generation
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestCacheKeyGeneration_DefaultChunkSize)
{
    // Test with default chunk size (should not include chunk size suffix)
    std::string cacheKey = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, false);
    
    // Should be a valid SHA256 hash (64 hex characters)
    EXPECT_EQ(cacheKey.length(), 64U);
    
    // Should be consistent
    std::string cacheKey2 = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, false);
    EXPECT_EQ(cacheKey, cacheKey2);
}

TEST_F(GetCacheChunkPropertiesTest, TestCacheKeyGeneration_CustomChunkSize)
{
    // Test with custom chunk size (should include chunk size suffix)
    std::string cacheKey = GenerateCacheKey(accountName, containerName, blobPath, 0, customChunkSize, false);
    
    // Should be a valid SHA256 hash
    EXPECT_EQ(cacheKey.length(), 64U);
    
    // Should be different from default chunk size key
    std::string defaultKey = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, false);
    EXPECT_NE(cacheKey, defaultKey);
}

TEST_F(GetCacheChunkPropertiesTest, TestCacheKeyGeneration_IgnoreAccount)
{
    // Test with ignore account name flag
    std::string cacheKey = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, true);
    
    // Should be valid
    EXPECT_EQ(cacheKey.length(), 64U);
    
    // Should be different from normal key
    std::string normalKey = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, false);
    EXPECT_NE(cacheKey, normalKey);
    
    // Should be same for different account names when ignoring account
    std::string differentAccountKey = GenerateCacheKey("differentaccount", containerName, blobPath, 0, defaultChunkSize, true);
    EXPECT_EQ(cacheKey, differentAccountKey);
}

TEST_F(GetCacheChunkPropertiesTest, TestCacheKeyGeneration_DifferentOffsets)
{
    // Different offsets should produce different cache keys
    std::string key1 = GenerateCacheKey(accountName, containerName, blobPath, 0, defaultChunkSize, false);
    std::string key2 = GenerateCacheKey(accountName, containerName, blobPath, defaultChunkSize, defaultChunkSize, false);
    
    EXPECT_NE(key1, key2);
}


//---------------------------------------------------------------------------------------------------------------------
// Integration test - complete workflow
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestCompleteWorkflow)
{
    // Test parameters
    int64_t offset = 0;
    uint32_t chunkSize = defaultChunkSize;
    bool ignoreAccount = false;
    uint32_t numServers = 4;
    
    // Generate cache key
    std::string cacheKey = GenerateCacheKey(accountName, containerName, blobPath, offset, chunkSize, ignoreAccount);
    EXPECT_EQ(cacheKey.length(), 64U);
 
    // Calculate server index
    uint32_t serverIndex = GetServerIndex(cacheKey.c_str(), numServers);
    EXPECT_LT(serverIndex, numServers);
    
    // Verify consistency
    std::string cacheKey2 = GenerateCacheKey(accountName, containerName, blobPath, offset, chunkSize, ignoreAccount);
    uint32_t serverIndex2 = GetServerIndex(cacheKey2.c_str(), numServers);
    
    EXPECT_EQ(cacheKey, cacheKey2);
    EXPECT_EQ(serverIndex, serverIndex2);
}

//---------------------------------------------------------------------------------------------------------------------
// Test ParseBool function
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestParseBool)
{
    // Test true values
    EXPECT_TRUE(ParseBool("true"));
    EXPECT_TRUE(ParseBool("1"));
    EXPECT_TRUE(ParseBool("yes"));
    
    // Test false values
    EXPECT_FALSE(ParseBool("false"));
    EXPECT_FALSE(ParseBool("0"));
    EXPECT_FALSE(ParseBool("no"));
    
    // Test invalid values
    EXPECT_THROW(ParseBool("invalid"), std::invalid_argument);
    EXPECT_THROW(ParseBool("TRUE"), std::invalid_argument); // Case sensitive
    EXPECT_THROW(ParseBool(""), std::invalid_argument);
}

//---------------------------------------------------------------------------------------------------------------------
// Edge case tests
//---------------------------------------------------------------------------------------------------------------------
TEST_F(GetCacheChunkPropertiesTest, TestEdgeCases)
{
    // Test with empty strings
    std::string emptyKey = GenerateCacheKey("", "", "", 0, defaultChunkSize, false);
    EXPECT_EQ(emptyKey.length(), 64U); // Should still produce valid hash
    
    // Test with very large offset
    int64_t largeOffset = 1000000000000LL; // 1TB
    std::string largeOffsetKey = GenerateCacheKey(accountName, containerName, blobPath, largeOffset, defaultChunkSize, false);
    EXPECT_EQ(largeOffsetKey.length(), 64U);
    
    // Test with very small chunk size
    uint32_t smallChunkSize = 1024; // 1KB
    std::string smallChunkKey = GenerateCacheKey(accountName, containerName, blobPath, 0, smallChunkSize, false);
    EXPECT_EQ(smallChunkKey.length(), 64U);
    
    // Test server index with large number of servers
    uint32_t manyServers = 1000;
    uint32_t serverIndex = GetServerIndex(blobPath.c_str(), manyServers);
    EXPECT_LT(serverIndex, manyServers);
}
//---------------------------------------------------------------------------------------------------------------------