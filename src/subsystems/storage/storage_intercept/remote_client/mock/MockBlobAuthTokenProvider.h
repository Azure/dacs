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
    long GetStatusCode(CURL*) override { return mockStatusCode; }
    void SetStatusCode(long code) { mockStatusCode = code; }
};
