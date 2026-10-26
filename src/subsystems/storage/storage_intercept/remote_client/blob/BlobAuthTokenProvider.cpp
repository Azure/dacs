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

#include "BlobAuthTokenProvider.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include "StorageInterceptLogger.h"
#include "RemoteClientException.h"
#include <optional>

BlobAuthTokenProvider::BlobAuthTokenProvider()
    : m_rng(std::random_device{}()),
      m_tokenRefreshBufferSeconds(StorageInterceptConfig::GetInstance().GetTokenRefreshBufferInSeconds()),
      m_tokenExpiryBufferSeconds(StorageInterceptConfig::GetInstance().GetTokenExpiryBufferInSeconds()),
      m_enableSSLVerification(StorageInterceptConfig::GetInstance().GetEnableSSLVerification())
{
    LOG_INFO("BlobAuthTokenProvider initialized with token refresh buffer: {} seconds, "
             "token expiry buffer: {} seconds, SSL verification enabled: {}",
             m_tokenRefreshBufferSeconds, 
             m_tokenExpiryBufferSeconds, 
             m_enableSSLVerification);  

}
//---------------------------------------------------------------------------------------------------------------------
BlobAuthTokenProvider::~BlobAuthTokenProvider()
{

    std::vector<std::shared_future<void>> futuresToWait;

    {
        std::lock_guard<std::mutex> lock(m_tokenUpdateLock);

        for (auto& future : m_mapBgTokenUpdateFutures) {
            if (future.second.valid()) {
                futuresToWait.push_back(std::move(future.second));
            }
        }

        m_mapBgTokenUpdateFutures.clear();
    }

    for (auto& future : futuresToWait) {
        future.wait(); 
    }
}
//---------------------------------------------------------------------------------------------------------------------
BlobAuthTokenProvider& BlobAuthTokenProvider::GetInstance()
{
    static BlobAuthTokenProvider s_blobAuthTokenProviderInstance;
    return s_blobAuthTokenProviderInstance;
}
//---------------------------------------------------------------------------------------------------------------------
/* static */ bool BlobAuthTokenProvider::HasCompleteMSIConfig(
    const std::string& msiEndpoint,
    const std::string& msiSecret,
    const std::string& identityClientId)
{
    return !msiEndpoint.empty() && !msiSecret.empty() && !identityClientId.empty();
}
//---------------------------------------------------------------------------------------------------------------------
// Function to retrieve the access token for a given mount path
std::string BlobAuthTokenProvider::GetAccessToken(const std::string& mountPath)
{
    return GetAccessTokenInfo(mountPath).token;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to retrieve the access token together with its real expiry for a given mount path
TokenInfo BlobAuthTokenProvider::GetAccessTokenInfo(const std::string& mountPath)
{
    LOG_DEBUG("Get access token for mount path: {}", mountPath);

    std::unique_lock<std::mutex> lock(m_tokenUpdateLock);
    //token exists, is valid and not nearing expiry, return it
    if(m_mapMountPathToken.find(mountPath) != m_mapMountPathToken.end() && !IsTokenNearingExpiry(m_mapMountPathToken[mountPath]))
    {
        return m_mapMountPathToken[mountPath];
    }

    //token does not exist or is nearing expiry, start a background update if not already in progress
    if(!m_mapBgTokenUpdateInProgress[mountPath])
    {
        m_mapBgTokenUpdateInProgress[mountPath] = true;
        m_mapBgTokenUpdateFutures[mountPath] =
            std::async(std::launch::async, &BlobAuthTokenProvider::PerformTokenUpdate, this, mountPath);
    }
    else
    {
        LOG_DEBUG("Background token update already in progress for mount path: {}", mountPath);
    }

    // token exists, is nearing expiry but not expired, return it as background update already in started/in progress
    if(m_mapMountPathToken.find(mountPath) != m_mapMountPathToken.end() && !IsTokenExpired(m_mapMountPathToken[mountPath]))
    {
        LOG_DEBUG("Returning cached access token for mount path: {}", mountPath);
        return m_mapMountPathToken[mountPath];
    }

    //store the future and release the lock before waiting for it to finish
    auto tokenFetchFuture = m_mapBgTokenUpdateFutures[mountPath];
    lock.unlock();
 
    //token does not exist or is expired, wait for the background update to finish
    tokenFetchFuture.wait(); 

    lock.lock();
    if (m_mapMountPathToken.find(mountPath) == m_mapMountPathToken.end()) {
        LOG_ERROR("Token not found in map after background update for mount path: {}", mountPath);
        throw RemoteClientException("TokenFetchError: Token not available after background update");
    }
    return m_mapMountPathToken[mountPath];
}
//---------------------------------------------------------------------------------------------------------------------
// Function to start a background process to update the token
void BlobAuthTokenProvider::PerformTokenUpdate(const std::string& mountPath) {
    LOG_INFO("Background token update started for mount path: {}", mountPath);

    try {
        TokenInfo newTokenInfo = FetchNewAccessToken(mountPath);
        std::lock_guard<std::mutex> lock(m_tokenUpdateLock);
        m_mapMountPathToken[mountPath] = newTokenInfo;
        m_mapBgTokenUpdateInProgress[mountPath] = false;

        LOG_INFO("Background token update completed for mount path: {}", mountPath);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception occurred during background token update for mount path {}: {}", mountPath, e.what());
        std::lock_guard<std::mutex> lock(m_tokenUpdateLock);
        m_mapBgTokenUpdateInProgress[mountPath] = false;
    } catch (...) {
        LOG_ERROR("Unknown exception occurred during background token update for mount path: {}", mountPath);
        std::lock_guard<std::mutex> lock(m_tokenUpdateLock);
        m_mapBgTokenUpdateInProgress[mountPath] = false;
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Detect whether to use MSI, Workload Identity, or Azure Identity SDK based on available config
TokenFetchMode BlobAuthTokenProvider::DetectTokenFetchMode(const std::string& mountPath)
{
    std::string federatedTokenFile = StorageInterceptConfig::GetInstance().GetFederatedTokenFile(mountPath);
    std::string tenantId = StorageInterceptConfig::GetInstance().GetTenantId(mountPath);

    if (!federatedTokenFile.empty() && !tenantId.empty())
    {
        LOG_INFO("Workload identity config detected for mount path: {}", mountPath);
        return TokenFetchMode::WorkloadIdentity;
    }

    std::string msiEndpoint = StorageInterceptConfig::GetInstance().GetMSIEndpoint(mountPath);
    std::string msiSecret = StorageInterceptConfig::GetInstance().GetMSISecret(mountPath);
    std::string identityClientId = StorageInterceptConfig::GetInstance().GetIdentityClientId(mountPath);

    if (HasCompleteMSIConfig(msiEndpoint, msiSecret, identityClientId))
    {
        LOG_INFO("MSI config detected for mount path: {}", mountPath);
        return TokenFetchMode::MSI;
    }

    LOG_INFO("No MSI or workload identity config found for mount path: {}, falling back to Azure Identity SDK", mountPath);
    return TokenFetchMode::AzureIdentitySDK;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to fetch new access token for a given mount path (dispatches to MSI, Workload Identity, or Azure Identity SDK)
TokenInfo BlobAuthTokenProvider::FetchNewAccessToken(const std::string& mountPath)
{
    LOG_INFO("Fetching new access token for mount path: {}", mountPath);

    TokenFetchMode mode = DetectTokenFetchMode(mountPath);

    if (mode == TokenFetchMode::WorkloadIdentity)
    {
        return FetchTokenViaWorkloadIdentity(mountPath);
    }
    else if (mode == TokenFetchMode::MSI)
    {
        return FetchTokenViaMSI(mountPath);
    }
    else
    {
        return FetchTokenViaAzureIdentitySDK();
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Read the projected service account token from the file injected by the AKS workload identity webhook
std::string BlobAuthTokenProvider::ReadFederatedTokenFile(const std::string& filePath)
{
    // The path comes from configuration. Constrain it to an absolute path with no parent-directory
    // traversal so it cannot be pointed at arbitrary files outside the projected token mount.
    if (filePath.empty() || filePath.front() != '/' ||
        filePath.find("/../") != std::string::npos ||
        (filePath.size() >= 3 && filePath.compare(filePath.size() - 3, 3, "/..") == 0))
    {
        throw RemoteClientException("WorkloadIdentityError: Invalid federated token file path: " + filePath);
    }

    std::ifstream tokenFile(filePath);
    if (!tokenFile.is_open())
    {
        throw RemoteClientException("WorkloadIdentityError: Failed to open federated token file: " + filePath);
    }

    std::string token((std::istreambuf_iterator<char>(tokenFile)),
                       std::istreambuf_iterator<char>());

    // Trim trailing whitespace/newlines
    while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' '))
    {
        token.pop_back();
    }

    if (token.empty())
    {
        throw RemoteClientException("WorkloadIdentityError: Federated token file is empty: " + filePath);
    }

    // Ensure we forward a bearer assertion, not the contents of an unrelated file.
    if (!StorageInterceptUtils::LooksLikeJwt(token))
    {
        throw RemoteClientException(
            "WorkloadIdentityError: Federated token file does not contain a valid token: " + filePath);
    }

    return token;
}
//---------------------------------------------------------------------------------------------------------------------
// URL-encode a string using libcurl
std::string BlobAuthTokenProvider::UrlEncode(CURL* curl, const std::string& value)
{
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    if (!encoded)
    {
        throw RemoteClientException("CurlError: Failed to URL-encode value");
    }
    std::string result(encoded);
    curl_free(encoded);
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
// Common retry helper: executes httpRequestFn on each attempt with exponential backoff + jitter.
// On HTTP 200, delegates to responseHandlerFn to parse the response and return TokenInfo.
// On non-retryable errors, throws immediately. On retryable errors, retries up to MAX_RETRIES.
TokenInfo BlobAuthTokenProvider::ExecuteWithRetry(
    const std::string& context,
    std::function<HttpResponse(int attempt)> httpRequestFn,
    std::function<TokenInfo(const std::string& responseData)> responseHandlerFn)
{
    uint32_t attempt = 1;

    while (attempt <= MAX_RETRIES)
    {
        LOG_INFO("{}: attempt {}/{}", context, attempt, MAX_RETRIES);
        HttpResponse response = httpRequestFn(static_cast<int>(attempt));

        if (response.curlCode == CURLE_OK && response.statusCode == 200)
        {
            return responseHandlerFn(response.responseData);
        }

        if (!IsRetryableError(response.curlCode, response.statusCode))
        {
            if (response.curlCode == CURLE_OK)
            {
                LOG_ERROR("{}: non-retryable HTTP status: {}, response: {}",
                          context, response.statusCode, response.responseData);
                throw RemoteClientException(
                    "TokenFetchError: " + context + " failed with HTTP status: " +
                    std::to_string(response.statusCode));
            }
            else
            {
                LOG_ERROR("{}: non-retryable curl error: {} ({})",
                          context, curl_easy_strerror(response.curlCode), static_cast<int>(response.curlCode));
                throw RemoteClientException(
                    "CurlError: " + context + ": " + std::string(curl_easy_strerror(response.curlCode)));
            }
        }

        attempt++;
        if (attempt > MAX_RETRIES)
        {
            LOG_ERROR("{}: max retries {} exhausted", context, MAX_RETRIES);
            break;
        }

        // Exponential backoff with jitter
        LOG_WARN("{}: attempt {} failed, retrying...", context, attempt - 1);
        const uint32_t interval_without_jitter = BASE_DELAY_SEC << attempt;
        std::uniform_int_distribution<uint32_t> dist(1, std::min(MAX_DELAY_SEC, interval_without_jitter));
        auto interval_with_jitter = std::chrono::seconds(dist(m_rng));
        LOG_INFO("{}: will retry in {} seconds", context, interval_with_jitter.count());
        std::this_thread::sleep_for(interval_with_jitter);
    }

    throw RemoteClientException("TokenFetchError: Max retries reached for " + context);
}
//---------------------------------------------------------------------------------------------------------------------
// Fetch token via AKS Workload Identity (OAuth 2.0 client credentials with federated assertion)
TokenInfo BlobAuthTokenProvider::FetchTokenViaWorkloadIdentity(const std::string& mountPath)
{
    LOG_INFO("Fetching access token via Workload Identity for mount path: {}", mountPath);

    const std::string federatedTokenFile = StorageInterceptConfig::GetInstance().GetFederatedTokenFile(mountPath);
    const std::string tenantId = StorageInterceptConfig::GetInstance().GetTenantId(mountPath);
    const std::string clientId = StorageInterceptConfig::GetInstance().GetIdentityClientId(mountPath);
    const std::string authorityHost = StorageInterceptConfig::GetInstance().GetAuthorityHost(mountPath);

    if (federatedTokenFile.empty() || tenantId.empty() || clientId.empty())
    {
        LOG_ERROR("Workload identity config incomplete for mount path: {} "
                  "(federatedTokenFile={}, tenantId={}, clientId={})",
                  mountPath,
                  federatedTokenFile.empty() ? "<empty>" : "<set>",
                  tenantId.empty() ? "<empty>" : "<set>",
                  clientId.empty() ? "<empty>" : "<set>");
        throw RemoteClientException(
            "ConfigurationError: Workload identity requires azBlobFederatedTokenFile, "
            "azBlobTenantId, and azBlobIdentityClientId for mount path " + mountPath);
    }

    // Build the AAD token endpoint URL. IsAllowedAuthorityUrl only validates the scheme and host,
    // so normalize a trailing slash and reject any remaining path/query/fragment here; otherwise
    // an authorityHost like "https://login.microsoftonline.com/" would produce a malformed token
    // URL with a double slash (or, for a path, a URL pointing outside the validated host).
    std::string normalizedAuthorityHost = authorityHost;
    if (!normalizedAuthorityHost.empty() && normalizedAuthorityHost.back() == '/')
    {
        normalizedAuthorityHost.pop_back();
    }
    const size_t schemePos = normalizedAuthorityHost.find("://");
    if (schemePos == std::string::npos ||
        normalizedAuthorityHost.find('/', schemePos + 3) != std::string::npos ||
        normalizedAuthorityHost.find_first_of("?#") != std::string::npos)
    {
        LOG_ERROR("Rejecting workload identity request for authority host with path/query/fragment: {}",
                   authorityHost);
        throw RemoteClientException(
            "SecurityError: Configured authority host must not include a path/query/fragment: " + authorityHost);
    }
    if (!StorageInterceptUtils::IsAllowedAuthorityUrl(normalizedAuthorityHost))
    {
        LOG_ERROR("Rejecting workload identity request for disallowed authority host: {}", authorityHost);
        throw RemoteClientException(
            "SecurityError: Configured authority host is not a permitted Azure AD endpoint: " + authorityHost);
    }
    std::string tokenUrl = normalizedAuthorityHost + "/" + tenantId + "/oauth2/v2.0/token";

    auto httpRequestFn = [&](int) -> HttpResponse
    {
        // Re-read the federated token on each attempt (it can be rotated by kubelet)
        std::string federatedToken = ReadFederatedTokenFile(federatedTokenFile);

        auto encodeCurl = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(curl_easy_init(), &curl_easy_cleanup);
        if (!encodeCurl)
        {
            throw RemoteClientException("CurlError: Failed to initialize libcurl for URL encoding");
        }

        std::string postData =
            "grant_type=" + UrlEncode(encodeCurl.get(), "client_credentials") +
            "&client_id=" + UrlEncode(encodeCurl.get(), clientId) +
            "&client_assertion_type=" + UrlEncode(encodeCurl.get(), "urn:ietf:params:oauth:client-assertion-type:jwt-bearer") +
            "&client_assertion=" + UrlEncode(encodeCurl.get(), federatedToken) +
            "&scope=" + UrlEncode(encodeCurl.get(), std::string(STORAGE_RESOURCE) + "/.default");

        return PerformCurlPostRequest(tokenUrl, postData);
    };

    auto responseHandlerFn = [&](const std::string& responseData) -> TokenInfo
    {
        Json::Value jsonResponse;
        Json::CharReaderBuilder readerBuilder;
        std::string errors;
        std::istringstream responseStream(responseData);
        if (!Json::parseFromStream(readerBuilder, responseStream, &jsonResponse, &errors))
        {
            LOG_WARN("Failed to parse workload identity JSON response: {}", errors);
            throw RemoteClientException("JsonError: Failed to parse workload identity token response");
        }

        if (!jsonResponse.isMember("access_token") || !jsonResponse.isMember("expires_in"))
        {
            LOG_ERROR("Invalid workload identity token response: missing required fields (access_token={}, expires_in={})",
                      jsonResponse.isMember("access_token") ? "<set>" : "<empty>",
                      jsonResponse.isMember("expires_in") ? "<set>" : "<empty>");
            throw RemoteClientException(
                "TokenFetchError: Invalid workload identity token response: missing required fields");
        }

        std::string accessToken = jsonResponse["access_token"].asString();
        int64_t expiresIn = jsonResponse["expires_in"].asInt64();
        auto expiryTime = std::chrono::system_clock::now() + std::chrono::seconds(expiresIn);

        LOG_INFO("Successfully fetched workload identity access token for mountPath: {} expires in {} seconds",
                 mountPath, expiresIn);
        return { accessToken, expiryTime };
    };

    return ExecuteWithRetry("WorkloadIdentity[" + mountPath + "]", httpRequestFn, responseHandlerFn);
}
//---------------------------------------------------------------------------------------------------------------------
// Fetch token via Azure Identity SDK credential chain.
// Used as a fallback when neither workload identity nor MSI params are configured.
// Attempts credentials in order:
//   1. EnvironmentCredential (AZURE_CLIENT_ID / AZURE_CLIENT_SECRET / AZURE_TENANT_ID)
//   2. WorkloadIdentityCredential (AZURE_FEDERATED_TOKEN_FILE)
//   3. ManagedIdentityCredential (IMDS)
//
// Not using DefaultAzureCredential as it can get flagged by security scanners as risky due to its broad credential search.
TokenInfo BlobAuthTokenProvider::FetchTokenViaAzureIdentitySDK()
{
    LOG_INFO("Fetching access token via Azure Identity SDK credential chain");

    // Configure retry policy to match other token fetch methods
    Azure::Core::Http::Policies::RetryOptions retryOptions;
    retryOptions.MaxRetries = MAX_RETRIES;
    retryOptions.RetryDelay = std::chrono::seconds(BASE_DELAY_SEC);
    retryOptions.MaxRetryDelay = std::chrono::seconds(MAX_DELAY_SEC);

    Azure::Core::Credentials::TokenRequestContext requestContext;
    requestContext.Scopes = { std::string(STORAGE_RESOURCE) + "/.default" };

    // Use a deadline context matching the HTTP timeout used by curl-based token fetches
    Azure::Core::Context rootCtx;
    auto ctx = rootCtx.WithDeadline(
        std::chrono::system_clock::now() + std::chrono::seconds(HTTP_TIMEOUT_SECONDS));

    // Helper: attempt a single credential, return TokenInfo on success or std::nullopt on auth failure.
    auto tryCredential = [&](auto& credential, const std::string& credentialName)
        -> std::optional<TokenInfo>
    {
        try
        {
            Azure::Core::Credentials::AccessToken token = credential.GetToken(requestContext, ctx);
            // Note: AccessToken.ExpiresOn is Azure::DateTime which has different epoch than
            //       std::chrono::system_clock::time_point, so we convert it.
            auto expiryTime = static_cast<std::chrono::system_clock::time_point>(token.ExpiresOn);

            LOG_INFO("Successfully fetched access token via {}, expires at: {} seconds since epoch",
                     credentialName,
                     std::chrono::duration_cast<std::chrono::seconds>(expiryTime.time_since_epoch()).count());

            return TokenInfo{ token.Token, expiryTime };
        }
        catch (const Azure::Core::Credentials::AuthenticationException& e)
        {
            LOG_INFO("{} failed: {}", credentialName, e.what());
            return std::nullopt;
        }
    };

    // 1. EnvironmentCredential
    {
        Azure::Identity::EnvironmentCredentialOptions opts;
        opts.Retry = retryOptions;
        Azure::Identity::EnvironmentCredential cred(opts);
        if (auto result = tryCredential(cred, "EnvironmentCredential"))
            return *result;
    }

    // 2. WorkloadIdentityCredential
    {
        Azure::Identity::WorkloadIdentityCredentialOptions opts;
        opts.Retry = retryOptions;
        Azure::Identity::WorkloadIdentityCredential cred(opts);
        if (auto result = tryCredential(cred, "WorkloadIdentityCredential"))
            return *result;
    }

    // 3. ManagedIdentityCredential
    {
        Azure::Identity::ManagedIdentityCredentialOptions opts;
        opts.Retry = retryOptions;
        Azure::Identity::ManagedIdentityCredential cred(opts);
        if (auto result = tryCredential(cred, "ManagedIdentityCredential"))
            return *result;
    }

    LOG_ERROR("All Azure Identity SDK credentials failed");
    throw RemoteClientException("AzureIdentityError: All credentials (Environment, WorkloadIdentity, ManagedIdentity) failed");
}
//---------------------------------------------------------------------------------------------------------------------
// Fetch token via traditional MSI/IMDS endpoint
TokenInfo BlobAuthTokenProvider::FetchTokenViaMSI(const std::string& mountPath)
{
    LOG_INFO("Fetching new access token via MSI for mount path: {}", mountPath);

    std::string msiEndpoint = StorageInterceptConfig::GetInstance().GetMSIEndpoint(mountPath);   
    std::string msiSecret = StorageInterceptConfig::GetInstance().GetMSISecret(mountPath);
    std::string identityClientId = StorageInterceptConfig::GetInstance().GetIdentityClientId(mountPath);

    if (!HasCompleteMSIConfig(msiEndpoint, msiSecret, identityClientId))
    {
        LOG_ERROR("MSI endpoint, secret or client ID is empty for mount path: {}", mountPath);
        throw RemoteClientException("ConfigurationError: MSI endpoint, secret or client ID is empty for mount path " + mountPath);
    }

    std::string url = GenerateMSIUrl(msiEndpoint, identityClientId);

    auto httpRequestFn = [&](int /*attempt*/) -> HttpResponse
    {
        return PerformCurlRequestWithHeaders(url, msiSecret);
    };

    auto responseHandlerFn = [&](const std::string& responseData) -> TokenInfo
    {
        Json::Value jsonResponse;
        Json::CharReaderBuilder readerBuilder;
        std::string errors;
        std::istringstream responseStream(responseData);
        if (!Json::parseFromStream(readerBuilder, responseStream, &jsonResponse, &errors))
        {
            LOG_WARN("Failed to parse MSI JSON response: {}", errors);
            throw RemoteClientException("JsonError: Failed to parse JSON response");
        }

        if (!jsonResponse.isMember("access_token") || !jsonResponse.isMember("expires_on"))
        {
            LOG_ERROR("Invalid MSI token response: missing required fields");
            throw RemoteClientException("TokenFetchError: Invalid token response: missing required fields");
        }

        std::string accessToken = jsonResponse["access_token"].asString();
        int64_t expiresOn = std::stoll(jsonResponse["expires_on"].asString());
        auto expiryTime = std::chrono::system_clock::time_point(std::chrono::seconds(expiresOn));

        LOG_INFO("Successfully fetched MSI access token for mountPath: {}", mountPath);
        return { accessToken, expiryTime };
    };

    return ExecuteWithRetry("MSI[" + mountPath + "]", httpRequestFn, responseHandlerFn);
}

//---------------------------------------------------------------------------------------------------------------------
// Function to check if the token is expired
bool BlobAuthTokenProvider::IsTokenExpired(TokenInfo& tokenInfo)
{
    auto now = std::chrono::system_clock::now();
    auto expiryTime = tokenInfo.expiryTime;
    auto timeToExpiry = std::chrono::duration_cast<std::chrono::seconds>(expiryTime - now);
    auto expiryBufferTime = std::chrono::seconds(m_tokenExpiryBufferSeconds);

    if (timeToExpiry <= expiryBufferTime)
    {
        LOG_DEBUG("Token is expired or about to expire, time to expiry: {} seconds", timeToExpiry.count());
        return true;
    }
    else
    {
        LOG_DEBUG("Token is valid, time to expiry: {} seconds", timeToExpiry.count());
        return false;
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Function to check if token is nearing expiry
bool BlobAuthTokenProvider::IsTokenNearingExpiry(TokenInfo& tokenInfo)
{
    auto now = std::chrono::system_clock::now();
    auto expiryTime = tokenInfo.expiryTime;
    auto timeToExpiry = std::chrono::duration_cast<std::chrono::seconds>(expiryTime - now);
    auto refreshBufferTime = std::chrono::seconds(m_tokenRefreshBufferSeconds);

    if (timeToExpiry <= refreshBufferTime)
    {
        LOG_DEBUG("Token is nearing expiry, time to expiry: {} seconds", timeToExpiry.count());
        return true;
    }
    else
    {
        LOG_DEBUG("Token is not nearing expiry, time to expiry: {} seconds", timeToExpiry.count());
        return false;
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Function to generate the MSI URL 
std::string BlobAuthTokenProvider::GenerateMSIUrl(const std::string& msiEndpoint, const std::string& identityClientId)
{
    std::ostringstream urlStream;
    urlStream << msiEndpoint << "?api-version=2019-08-01&resource=" << STORAGE_RESOURCE
              << "&client_id=" << identityClientId;
    LOG_DEBUG("Generated MSI URL: {}", urlStream.str());

    return urlStream.str();
}
//---------------------------------------------------------------------------------------------------------------------
// Function to handle the libcurl write callback
size_t BlobAuthTokenProvider::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    if (!contents || !userp)
    {
        return 0;
    }

    // Append the received data to the string provided in userp
    std::string* responseData = static_cast<std::string*>(userp);
    responseData->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to get the HTTP status code from the curl handle
long BlobAuthTokenProvider::GetStatusCode(CURL* curl)
{
    long httpCode = 0;
    CURLcode res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (res != CURLE_OK) {
        LOG_ERROR("Failed to get HTTP status code: {}", curl_easy_strerror(res));
        return 0;  // Return 0 to indicate error getting status code
    }
    return httpCode;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to encapsulate curl_easy_perform
CURLcode BlobAuthTokenProvider::PerformCurlRequest(CURL* curl)
{
    try
    {
        return curl_easy_perform(curl);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception occurred during curl_easy_perform: {}", e.what());
        throw RemoteClientException("CurlError: Exception during curl_easy_perform");
    }
    catch (...)
    {
        LOG_ERROR("Unknown exception occurred during curl_easy_perform");
        throw RemoteClientException("CurlError: Unknown exception during curl_easy_perform");
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Function to setup the curl request and perform it with headers
HttpResponse BlobAuthTokenProvider::PerformCurlRequestWithHeaders(const std::string& url, const std::string& msiSecret)
{
    HttpResponse response = {CURLE_OK, 0, ""};
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        throw RemoteClientException("CurlError: Failed to initialize libcurl");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    
    // SSL configuration - enable or disable verification based on configuration
    if (!m_enableSSLVerification)
    {
        LOG_INFO("SSL verification is disabled");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Disable peer certificate verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // Disable hostname verification
    }
    
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);       // Max 5 redirects
    
    // Fixed timeout settings - liberal values for MSI endpoints
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);           // 3 minutes total timeout
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT_SECONDS); // 30 seconds connection timeout
    
    struct curl_slist* headers = nullptr;
    std::string secretHeader = "secret: " + msiSecret;
    headers = curl_slist_append(headers, "Metadata: true");
    headers = curl_slist_append(headers, secretHeader.c_str());
    if (!headers)
    {
        curl_easy_cleanup(curl);
        throw RemoteClientException("CurlError: Failed to create header list");
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.responseData);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, &response.responseData);

    response.curlCode = PerformCurlRequest(curl);
    response.statusCode = GetStatusCode(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    LOG_INFO("CURL response code: {}, http status code: {}", static_cast<int>(response.curlCode), response.statusCode);
    return response;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to perform a POST request (used for workload identity token exchange)
HttpResponse BlobAuthTokenProvider::PerformCurlPostRequest(const std::string& url, const std::string& postData)
{
    HttpResponse response = {CURLE_OK, 0, ""};
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl)
    {
        throw RemoteClientException("CurlError: Failed to initialize libcurl");
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(postData.length()));

    // SSL configuration
    if (!m_enableSSLVerification)
    {
        LOG_INFO("SSL verification is disabled");
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
    }

    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT_SECONDS);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (!headers)
    {
        // unique_ptr will handle cleanup
        throw RemoteClientException("CurlError: Failed to create header list");
    }

    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.responseData);

    response.curlCode = PerformCurlRequest(curl.get());
    response.statusCode = GetStatusCode(curl.get());

    curl_slist_free_all(headers);
    // unique_ptr will handle cleanup

    LOG_INFO("CURL POST response code: {}, http status code: {}", static_cast<int>(response.curlCode), response.statusCode);
    return response;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to check if the error is retryable
bool BlobAuthTokenProvider::IsRetryableError(CURLcode curlCode, long statusCode)
{
    if (curlCode == CURLE_OK)
    {
        // Check HTTP status codes
        return statusCode >= 500 || statusCode == 429 || statusCode == 410;
    }
    
    // Check CURL errors
    switch (curlCode)
    {
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_COULDNT_CONNECT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
            return true;
        default:
            return false;
    }
}
