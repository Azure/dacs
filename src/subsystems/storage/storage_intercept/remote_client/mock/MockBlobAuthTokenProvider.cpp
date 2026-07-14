// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#include "MockBlobAuthTokenProvider.h"
#include "RemoteClientException.h"
#include <random>
#include <chrono>

MockBlobAuthTokenProvider::MockBlobAuthTokenProvider() : BlobAuthTokenProvider() {}

CURLcode MockBlobAuthTokenProvider::PerformCurlRequest(CURL* curl) {
    char* url;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

    if (url == nullptr) {
        return CURLE_URL_MALFORMAT;
    }

    std::string urlStr(url);
    std::string clientId = urlStr.substr(urlStr.find("client_id=") + 10, urlStr.find("&", urlStr.find("client_id=")) - (urlStr.find("client_id=") + 10));

    if (clientId == "mock-client-id-failure") {

        //for attempt 1, return CURLE_COULDNT_CONNECT
        //for attempt 2, return http status code 500
        if (attemptCount == 0) {
            attemptCount++;
            return CURLE_COULDNT_CONNECT; // Simulate a connection error
        } else if (attemptCount == 1) {
            void* writeData = nullptr;
            curl_easy_getinfo(curl, CURLINFO_PRIVATE, &writeData);
            std::string* out = static_cast<std::string*>(writeData);
            if (out) {
                *out = R"({"error": "server_error", "error_description": "Internal server error"})";
            }
            SetStatusCode(500L);  // Store the response code
            attemptCount++;
            return CURLE_OK;
        }
    }

    std::string response;
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 9999);
    std::string token = "mock-access-token-" + std::to_string(dist(rng));

    if (!clientId.empty()) {
        // Set expiry time based on clientId
        auto now = std::chrono::system_clock::now();
        auto expiryTime = now + std::chrono::seconds(clientId == "mock-client-id-short-lived" ? 5 : 605);
        response = R"({"access_token": ")" + token + R"(", "expires_on": ")" +
                   std::to_string(std::chrono::duration_cast<std::chrono::seconds>(expiryTime.time_since_epoch()).count()) +
                   R"(", "token_type": "Bearer"})";
    } else {
        response = R"({"error": "invalid_client", "error_description": "Client ID is invalid"})";
    }

    void* writeData = nullptr;
    curl_easy_getinfo(curl, CURLINFO_PRIVATE, &writeData);

    std::string* out = static_cast<std::string*>(writeData);
    if (out) {
        *out = response;
    }

    SetStatusCode(200L);  // Set success code for normal responses
    attemptCount = 0; // Reset attempt count after successful request
    return CURLE_OK;
}

TokenInfo MockBlobAuthTokenProvider::FetchTokenViaAzureIdentitySDK()
{
    if (m_azureIdentitySDKShouldFail)
    {
        throw RemoteClientException("AzureIdentityError: DefaultAzureCredential failed: mock failure");
    }

    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 9999);
    std::string token = "mock-azure-identity-token-" + std::to_string(dist(rng));
    auto expiryTime = std::chrono::system_clock::now() + std::chrono::seconds(3600);
    return { token, expiryTime };
}