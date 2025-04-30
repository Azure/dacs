#pragma once

#include "../blob/BlobAuthTokenProvider.h"
#include <curl/curl.h>
#include <string>

class MockBlobAuthTokenProvider : public BlobAuthTokenProvider {
public:
    MockBlobAuthTokenProvider();
    CURLcode PerformCurlRequest(CURL* curl) override;

private:
    int attemptCount = 0;
    long mockStatusCode = 200;  

protected:
    long GetStatusCode(CURL* curl) override { return mockStatusCode; }  
    void SetStatusCode(long code) { mockStatusCode = code; }
};
