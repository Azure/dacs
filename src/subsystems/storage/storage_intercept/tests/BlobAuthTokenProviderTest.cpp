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

#include "BlobAuthTokenProviderTest.h"
#include "BlobAuthTokenProvider.h"
#include "DacsTokenCredential.h"
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
// DacsTokenCredential returns the provider's token and sets ExpiresOn = min(now + refreshBuffer,
// realExpiry) - so the SDK refreshes around when the token enters the provider's refresh window, and
// never past the token's real expiry.
TEST_F(BlobAuthTokenProviderTest, DacsTokenCredentialCapsExpiresOnAtRefreshBuffer)
{
    const std::string mountPath = "tokencred";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "http://mock-msi-endpoint");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "mock-secret");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");
    // refreshBuffer < the mock MSI token lifetime (605s) so the cached token is reused and ExpiresOn
    // takes the (now + refreshBuffer) branch of the min.
    const uint32_t refreshBufferSeconds = 500;
    OverrideConfig(StorageInterceptConstants::ConfigNameTokenRefreshBufferInSeconds, std::to_string(refreshBufferSeconds));

    MockBlobAuthTokenProvider mockProvider;
    // Prime the cache; the credential reuses this same cached token.
    const TokenInfo primed = mockProvider.GetAccessTokenInfo(mountPath);
    ASSERT_FALSE(primed.token.empty()) << "Mock provider returned an empty token";

    DacsTokenCredential credential(mountPath, mockProvider);

    const auto before = std::chrono::system_clock::now();
    Azure::Core::Credentials::AccessToken accessToken =
        credential.GetToken(Azure::Core::Credentials::TokenRequestContext{}, Azure::Core::Context{});
    const auto after = std::chrono::system_clock::now();

    // Token is sourced from the injected provider (cached token reused).
    ASSERT_EQ(accessToken.Token, primed.token);

    // refreshBuffer (500s) < token lifetime (605s) => ExpiresOn = now + refreshBuffer.
    const auto expiresOn = static_cast<std::chrono::system_clock::time_point>(accessToken.ExpiresOn);
    ASSERT_GE(expiresOn, before + std::chrono::seconds(refreshBufferSeconds) - std::chrono::seconds(2));
    ASSERT_LE(expiresOn, after + std::chrono::seconds(refreshBufferSeconds) + std::chrono::seconds(2));
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
// Workload identity: a configured authority host with a path/trailing-slash must be rejected
// before a token URL is built, rather than silently producing a malformed or out-of-allowlist URL.
TEST_F(BlobAuthTokenProviderTest, TestWorkloadIdentityRejectsAuthorityHostWithPath)
{
    MockBlobAuthTokenProvider mockProvider;
    std::string mountPath = "wli-authority-path";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "/var/run/secrets/token");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "tenant-id");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "mock-client-id");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobAuthorityHost).c_str(),
        "https://login.microsoftonline.com/evil-path");

    EXPECT_THROW(mockProvider.GetAccessToken(mountPath), RemoteClientException);
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
TEST_F(BlobAuthTokenProviderTest, TestAzureDateTimeToSystemClockConversion)
{
    auto now = std::chrono::system_clock::now();
    auto nowPlus1Hour = now + std::chrono::hours(1);
    Azure::DateTime azureDateTime = static_cast<Azure::DateTime>(nowPlus1Hour);

    // Old conversion method (using time_since_epoch)
    auto oldConversion = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(azureDateTime.time_since_epoch()));
    auto timeToExpiryOld = std::chrono::duration_cast<std::chrono::seconds>(oldConversion - now);
    // The Windows FILETIME epoch is 1601-01-01; Unix epoch is 1970-01-01 (~369 year gap).
    // On Linux, system_clock::duration is nanoseconds (int64_t). Multiplying the 100ns-tick
    // value by 100 overflows int64_t, so the wrapped result is ~215 years instead of ~369.
    // Either way the result is far in the future compared to a correct ~1-hour value.
    // Threshold is 100 years — well above any correctly-computed expiry (~1h) and well
    // below the empirically observed overflow result (~215 years = ~6.8e9 seconds).
    constexpr long long kEpochDiffThresholdSeconds = 100LL * 365 * 24 * 60 * 60;
    ASSERT_GT(timeToExpiryOld.count(), kEpochDiffThresholdSeconds) <<
        "Old conversion method should yield a time far in the future";

    // New conversion method (using static_cast)
    auto newConversion = static_cast<std::chrono::system_clock::time_point>(azureDateTime);
    auto timeToExpiryNew = std::chrono::duration_cast<std::chrono::seconds>(newConversion - now);
    ASSERT_NEAR(timeToExpiryNew.count(), 3600, 5) <<
        "New conversion method should yield a time approximately 1 hour in the future";
    
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(BlobAuthTokenProviderTest, TestAzureIdentitySDKTokenRefreshedAfterExpiry)
{
    MockBlobAuthTokenProvider mockProvider;
    mockProvider.m_azureIdentitySDKTokenLifetimeSeconds = 5; // short-lived token
    std::string mountPath = "sdk-expiry";
    // No MSI or workload identity config → forces AzureIdentitySDK path
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch initial Azure Identity SDK token";
    ASSERT_NE(token1.find("mock-azure-identity-token"), std::string::npos);

    // Wait for the 5-second token to expire
    std::this_thread::sleep_for(std::chrono::seconds(6));

    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token2.empty()) << "Failed to fetch Azure Identity SDK token after expiry";
    // If the bug were present, expiryTime would be ~year 2394 and token1 would be returned forever
    ASSERT_NE(token1, token2) << "Azure Identity SDK token was not refreshed after expiry";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(BlobAuthTokenProviderTest, TestAzureIdentitySDKTokenRefreshedWhenNearingExpiry)
{
    // Override refresh buffer BEFORE constructing the provider — m_tokenRefreshBufferSeconds is
    // captured from config in the constructor. A prior test may have set it to 2s, which would
    // make a 605s token never appear "nearing expiry" during the 8s wait window.
    OverrideConfig(StorageInterceptConstants::ConfigNameTokenRefreshBufferInSeconds, "600");
    MockBlobAuthTokenProvider mockProvider;
    // Token lifetime 605s, refresh buffer 600s → nearing expiry after 5s (605-600=5)
    mockProvider.m_azureIdentitySDKTokenLifetimeSeconds = 605;
    std::string mountPath = "sdk-near-expiry";
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobTenantId).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobMSISecret).c_str(), "");
    OverrideConfig((mountPath + StorageInterceptConstants::ConfigNameAzBlobIdentityClientId).c_str(), "");

    std::string token1 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token1.empty()) << "Failed to fetch initial Azure Identity SDK token";

    // Token becomes nearing-expiry after ~5s (605 - 600 = 5s remaining until refresh threshold).
    // Wait 8s to give margin on slow machines.
    std::this_thread::sleep_for(std::chrono::seconds(8));

    // Should still return the old token (not yet expired), but background refresh is triggered
    std::string token2 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token2.empty());
    ASSERT_EQ(token1, token2) << "Expected cached token while background refresh is in progress";

    // Give the background refresh time to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string token3 = mockProvider.GetAccessToken(mountPath);
    ASSERT_FALSE(token3.empty());
    // If the bug were present, token3 would still equal token1 because expiry was never triggered
    ASSERT_NE(token1, token3) << "Azure Identity SDK token was not refreshed in background when nearing expiry";
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
