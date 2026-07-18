#pragma once

#include "../blob/BlobAuthTokenProvider.h"
#include <curl/curl.h>
#include <string>

class MockBlobAuthTokenProvider : public BlobAuthTokenProvider {
public:
    MockBlobAuthTokenProvider();
    CURLcode PerformCurlRequest(CURL* curl) override;

    // Controls whether FetchTokenViaAzureIdentitySDK should simulate success or failure.
    // When set, the mock returns a fake token instead of calling the real SDK.
    bool m_azureIdentitySDKShouldFail = false;
    // Controls the lifetime (in seconds) of tokens returned by FetchTokenViaAzureIdentitySDK.
    int m_azureIdentitySDKTokenLifetimeSeconds = 3600;
    TokenInfo FetchTokenViaAzureIdentitySDK() override;

private:
    int attemptCount = 0;
    long mockStatusCode = 200;  

protected:
    long GetStatusCode(CURL* curl) override { return mockStatusCode; }  
    void SetStatusCode(long code) { mockStatusCode = code; }
};
