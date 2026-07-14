//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <mutex>
#include <chrono>
#include <thread>
#include <random>
#include <iostream>
#include <unordered_map>
#include <curl/curl.h> 
#include <sstream>
#include <future>
#include <fstream>
#include <functional>
#include <jsoncpp/json/json.h>
#include <azure/identity.hpp>
#ifdef STORAGE_INTERCEPT_UT
#include <gtest/gtest.h>
#endif

enum class TokenFetchMode
{
    MSI,              // Traditional managed identity via MSI endpoint (IMDS)
    WorkloadIdentity, // AKS workload identity via federated token exchange
    AzureIdentitySDK  // Fallback: Azure Identity SDK DefaultAzureCredential
};

struct TokenInfo
{
    std::string token;
    std::chrono::system_clock::time_point expiryTime;
};
struct HttpResponse
{
    CURLcode curlCode;
    long statusCode;
    std::string responseData;
};
class BlobAuthTokenProvider
{
public:
    static BlobAuthTokenProvider& GetInstance();

    // Function to fetch the access token for a given mount path
    std::string GetAccessToken(const std::string& mountPath);

    protected:
    BlobAuthTokenProvider(); 

    ~BlobAuthTokenProvider(); 

private:
    bool IsTokenExpired(TokenInfo& tokenInfo);
    bool IsTokenNearingExpiry(TokenInfo& tokenInfo);
    TokenInfo FetchNewAccessToken(const std::string& mountPath);
    TokenInfo FetchTokenViaMSI(const std::string& mountPath);
    TokenInfo FetchTokenViaWorkloadIdentity(const std::string& mountPath);
    virtual TokenInfo FetchTokenViaAzureIdentitySDK();
    TokenFetchMode DetectTokenFetchMode(const std::string& mountPath);
    static std::string ReadFederatedTokenFile(const std::string& filePath);
    static std::string UrlEncode(CURL* curl, const std::string& value);
    void PerformTokenUpdate(const std::string& mountPath);

    // Common retry helper: executes httpRequestFn on each attempt, then responseHandlerFn on success.
    // httpRequestFn(attempt) returns HttpResponse. responseHandlerFn(responseData) returns TokenInfo.
    TokenInfo ExecuteWithRetry(
        const std::string& context,
        std::function<HttpResponse(int attempt)> httpRequestFn,
        std::function<TokenInfo(const std::string& responseData)> responseHandlerFn);
    virtual CURLcode PerformCurlRequest(CURL* curl); 
    virtual long GetStatusCode(CURL* curl);
    HttpResponse PerformCurlRequestWithHeaders(const std::string& url, const std::string& msiSecret);
    HttpResponse PerformCurlPostRequest(const std::string& url, const std::string& postData);

    static bool HasCompleteMSIConfig(
        const std::string& msiEndpoint,
        const std::string& msiSecret,
        const std::string& identityClientId);
    static std::string GenerateMSIUrl(const std::string& msiEndpoint, const std::string& identityClientId);
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static bool IsRetryableError(CURLcode curlCode, long httpCode);

    // Member variables
    std::unordered_map<std::string, TokenInfo> m_mapMountPathToken;
    std::unordered_map<std::string, bool> m_mapBgTokenUpdateInProgress;
    std::unordered_map<std::string, std::shared_future<void>> m_mapBgTokenUpdateFutures;
    std::mutex m_tokenUpdateLock;
    std::mt19937 m_rng; 

    const int m_tokenRefreshBufferSeconds;
    const int m_tokenExpiryBufferSeconds;
    const bool m_enableSSLVerification;

    static constexpr uint32_t MAX_DELAY_SEC = 64;
    static constexpr uint32_t MAX_RETRIES = 5;
    static constexpr uint32_t BASE_DELAY_SEC = 1;
    static constexpr const char* STORAGE_RESOURCE = "https://storage.azure.com";
    
    static constexpr long HTTP_TIMEOUT_SECONDS = 180L;        // 3 minutes total timeout
    static constexpr long CONNECTION_TIMEOUT_SECONDS = 30L;   // 30 seconds connection timeout

#ifdef STORAGE_INTERCEPT_UT
    FRIEND_TEST(BlobAuthTokenProviderTest, TestDetectTokenFetchModeWorkloadIdentity);
    FRIEND_TEST(BlobAuthTokenProviderTest, TestDetectTokenFetchModeMSI);
    FRIEND_TEST(BlobAuthTokenProviderTest, TestDetectTokenFetchModeAzureIdentitySDK);
#endif
};
