//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "BlobAuthTokenProviderTest.h"
#include "BlobAuthTokenProvider.h"
#include "mock/MockBlobAuthTokenProvider.h"
#include "RemoteClientException.h"
#include "StorageInterceptConstants.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <curl/curl.h>
#include <random>
#include <mutex>

//---------------------------------------------------------------------------------------------------------------------
BlobAuthTokenProviderTest::BlobAuthTokenProviderTest() : TestBase()
{

}
//---------------------------------------------------------------------------------------------------------------------
void BlobAuthTokenProviderTest::SetUp()
{
    if (!m_bUsingMock)
    {
        GTEST_SKIP() << "Skipping tests as m_bUsingMock is false.";
    }
}
//---------------------------------------------------------------------------------------------------------------------
void BlobAuthTokenProviderTest::TearDown()
{

}
//---------------------------------------------------------------------------------------------------------------------
//Test case to fetch a fresh token from the MSI endpoint
TEST_F(BlobAuthTokenProviderTest, TestFetchAccessToken)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "";
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint, "http://mock-msi-endpoint");
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobMSISecret, "mock-secret");
    OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobIdentityClientId, "mock-client-id");
    std::string token = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token.empty()) << "Failed to fetch access token from MSI endpoint";
}
//---------------------------------------------------------------------------------------------------------------------
// Test case to check if token is returned from map
TEST_F(BlobAuthTokenProviderTest, TestGetAccessTokenFromMap)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");

    // Fetch the token for the first time
    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";

    // Fetch the token again and check if it is the same as the first one
    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_EQ(token1, token2) << "Token fetched from map does not match the expected value";

}
//---------------------------------------------------------------------------------------------------------------------
// Test case to check if token is updated when it has expired
TEST_F(BlobAuthTokenProviderTest, TestTokenUpdateAfterExpiry)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test2";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id-short-lived");

    // Fetch the token for the first time
    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";

    // wait for the token to expire
    // expiry time is set to 5 seconds in the mocked response, so we wait for 6 seconds

    std::this_thread::sleep_for(std::chrono::seconds(6));

    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token2.empty()) << "Failed to fetch access token from MSI endpoint after expiry";
    // token2 should be different from token1 as the token would be updated in the background
    ASSERT_NE(token1, token2) << "Token was not updated when expired";
}
//---------------------------------------------------------------------------------------------------------------------
// Test case to check if token is updated when it is about to expire
TEST_F(BlobAuthTokenProviderTest, TestTokenUpdateNearingExpiry)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test3";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");

    // Fetch the token for the first time
    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";

    // Wait for a duration such that token expiry falls within token refresh threshold
    // expiry time is set to 605 seconds in the mocked response, so we wait for 6 seconds
    // refresh threshold is 600 seconds
    std::this_thread::sleep_for(std::chrono::seconds(6));

    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token2.empty()) << "Failed to fetch access token from MSI endpoint after expiry";
    // token2 should be same as token1 as the token is not expired
    // but token is approaching expiry, so would be updated in the background
    ASSERT_EQ(token1, token2) << "Token fetched from map does not match the expected value";

    // simulate wait for the background thread to finish updating the token
    // but this time is still within the expiry time of the token
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string token3 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token3.empty()) << "Failed to fetch access token from MSI endpoint";
    // token3 should be different from token1 as the token would be updated in the background
    ASSERT_NE(token1, token3) << "Token was not updated when nearing expiry";
}
//---------------------------------------------------------------------------------------------------------------------
// Test case to check retry logic on failure
TEST_F(BlobAuthTokenProviderTest, TestRetryLogic)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test4";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id-failure");

    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";
}
//---------------------------------------------------------------------------------------------------------------------
// test to check thread safety of the token update process
TEST_F(BlobAuthTokenProviderTest, TestThreadSafety)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test5";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");


    // Create multiple threads to fetch tokens concurrently
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::set<std::string> tokens;
    std::mutex tokensMutex; // Add a mutex to protect the set

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&tokens, &tokensMutex, &mockProvider, mountPath]() {
            std::string token = mockProvider.GetAccessToken(mountPath);
            ASSERT_FALSE(token.empty()) << "Failed to fetch access token from MSI endpoint";
            std::lock_guard<std::mutex> lock(tokensMutex); // Lock the mutex before modifying the set
            tokens.insert(token);
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Check that all tokens fetched are the same
    ASSERT_EQ(tokens.size(), 1) << "Multiple different tokens fetched from MSI endpoint";
}
//---------------------------------------------------------------------------------------------------------------------
//Test case to check multiThreaded token nearing expiry
TEST_F(BlobAuthTokenProviderTest, TestMultiThreadedTokenNearingExpiry)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "test6";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");

    // Create multiple threads to fetch tokens concurrently
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::set<std::string> tokens;
    std::mutex tokensMutex; // Add a mutex to protect the set
    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";

    // Mock token lifetime is 605s, refresh buffer is 600s.
    // Token becomes "nearing expiry" when remaining ≤ 600s, i.e. after 5s elapsed.
    // Use 8s to give comfortable margin on slow CI machines (arm64).
    std::this_thread::sleep_for(std::chrono::seconds(8));

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&tokens, &tokensMutex, &mockProvider, mountPath]() {
            std::string token = mockProvider.GetAccessToken(mountPath);
            ASSERT_FALSE(token.empty()) << "Failed to fetch access token from MSI endpoint";
            std::lock_guard<std::mutex> lock(tokensMutex); // Lock the mutex before modifying the set
            tokens.insert(token);
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token2.empty()) << "Failed to fetch access token from MSI endpoint after expiry";
    tokens.insert(token2);
    //one token from cache and only one new fetched token from  all threads
    ASSERT_EQ(tokens.size(), 2) << "Multiple different tokens fetched from MSI endpoint";
}
//---------------------------------------------------------------------------------------------------------------------
//Test multithreaded token update after expiry
TEST_F(BlobAuthTokenProviderTest, TestMultiThreadedTokenUpdateAfterExpiry)
{
    
    std::string mountPath = "test7";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id-short-lived");
    OverrideConfig(StorageInterceptConstants::ConfigNameTokenRefreshBufferInSeconds, "2");
    MockBlobAuthTokenProvider mockProvider;
    // Create multiple threads to fetch tokens concurrently
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::set<std::string> tokens;
    std::mutex tokensMutex; // Add a mutex to protect the set

    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch access token from MSI endpoint";
    // wait for the token to expire
    // expiry time is set to 5 seconds in the mocked response, so we wait for 6 seconds
    std::this_thread::sleep_for(std::chrono::seconds(6));

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&tokens, &tokensMutex, &mockProvider, mountPath]() {
            std::string token = mockProvider.GetAccessToken(mountPath);
            ASSERT_FALSE(token.empty()) << "Failed to fetch access token from MSI endpoint";
            std::lock_guard<std::mutex> lock(tokensMutex); // Lock the mutex before modifying the set
            tokens.insert(token);
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Check that all tokens fetched are the same
    ASSERT_EQ(tokens.size(), 1) << "Multiple different tokens fetched from MSI endpoint";

}
//---------------------------------------------------------------------------------------------------------------------
// DetectTokenFetchMode: workload identity params present → WorkloadIdentity
TEST_F(BlobAuthTokenProviderTest, TestDetectTokenFetchModeWorkloadIdentity)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "wli-detect";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "/var/run/secrets/token");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "tenant-id");
    // Ensure MSI params are absent so we can confirm workload identity wins
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");

    ASSERT_EQ(mockProvider.DetectTokenFetchMode(mountPath), TokenFetchMode::WorkloadIdentity);
}
//---------------------------------------------------------------------------------------------------------------------
// DetectTokenFetchMode: MSI params present (no workload identity) → MSI
TEST_F(BlobAuthTokenProviderTest, TestDetectTokenFetchModeMSI)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "msi-detect";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");

    ASSERT_EQ(mockProvider.DetectTokenFetchMode(mountPath), TokenFetchMode::MSI);
}
//---------------------------------------------------------------------------------------------------------------------
// DetectTokenFetchMode: neither workload identity nor MSI params → AzureIdentitySDK
TEST_F(BlobAuthTokenProviderTest, TestDetectTokenFetchModeAzureIdentitySDK)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "sdk-detect";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    ASSERT_EQ(mockProvider.DetectTokenFetchMode(mountPath), TokenFetchMode::AzureIdentitySDK);
}
//---------------------------------------------------------------------------------------------------------------------
// Azure Identity SDK fallback: token is fetched when no MSI or workload identity config is present
TEST_F(BlobAuthTokenProviderTest, TestGetAccessTokenViaAzureIdentitySDK)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "sdk-fetch";
    // No MSI or workload identity config → forces AzureIdentitySDK path
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    std::string token = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token.empty()) << "Expected a token from Azure Identity SDK fallback";
    ASSERT_NE(token.find("mock-azure-identity-token"), std::string::npos)
        << "Token should come from the mocked Azure Identity SDK path";
}
//---------------------------------------------------------------------------------------------------------------------
// Azure Identity SDK fallback: cached token is reused on second call
TEST_F(BlobAuthTokenProviderTest, TestGetAccessTokenViaAzureIdentitySDKCached)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "sdk-cache";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    std::string token1 = mockProvider.GetAccessToken(mountPath);
    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_EQ(token1, token2) << "Second call should return the cached Azure Identity SDK token";
}
//---------------------------------------------------------------------------------------------------------------------
// Azure Identity SDK fallback: authentication failure propagates as RemoteClientException
TEST_F(BlobAuthTokenProviderTest, TestGetAccessTokenViaAzureIdentitySDKFailure)
{
    MockBlobAuthTokenProvider mockProvider;
    mockProvider.m_azureIdentitySDKShouldFail = true;
    std::string mountPath = "sdk-fail";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    ASSERT_THROW(mockProvider.GetAccessToken(mountPath), RemoteClientException);
}
