#include "BlobAuthTokenProvider.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "RemoteClientException.h" 


BlobAuthTokenProvider::BlobAuthTokenProvider()
    : m_rng(std::random_device{}()),
      m_tokenRefreshBufferSeconds(StorageInterceptConfig::GetInstance().GetTokenRefreshBufferInSeconds()),
      m_tokenExpiryBufferSeconds(StorageInterceptConfig::GetInstance().GetTokenExpiryBufferInSeconds())
{
    LOG_INFO("BlobAuthTokenProvider initialized with token refresh buffer: {} seconds, token expiry buffer: {} seconds",
             m_tokenRefreshBufferSeconds, m_tokenExpiryBufferSeconds);  

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
// Function to retrive the access token for a given mount path
std::string BlobAuthTokenProvider::GetAccessToken(const std::string& mountPath)
{
    LOG_DEBUG("Get access token for mount path: {}", mountPath);

    std::unique_lock<std::mutex> lock(m_tokenUpdateLock);
    //token exists, is valid and not nearing expiry, return it
    if(m_mapMountPathToken.find(mountPath) != m_mapMountPathToken.end() && !IsTokenNearingExpiry(m_mapMountPathToken[mountPath]))
    {
        return m_mapMountPathToken[mountPath].token;
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
        return m_mapMountPathToken[mountPath].token;
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
    return m_mapMountPathToken[mountPath].token;
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
// Function to fetch new access token for a given mount path
TokenInfo BlobAuthTokenProvider::FetchNewAccessToken(const std::string& mountPath)
{
    LOG_INFO("Fetching new access token for mount path: {}", mountPath);

    std::string msiEndpoint = StorageInterceptConfig::GetInstance().GetMSIEndpoint(mountPath);   
    std::string msiSecret = StorageInterceptConfig::GetInstance().GetMSISecret(mountPath);
    std::string identityClientId = StorageInterceptConfig::GetInstance().GetIdentityClientId(mountPath);

    if (msiEndpoint.empty() || msiSecret.empty() || identityClientId.empty())
    {
        LOG_ERROR("MSI endpoint, secret or client ID is empty for mount path: {}", mountPath);
        throw RemoteClientException("ConfigurationError: MSI endpoint, secret or client ID is empty for mount path " + mountPath);
    }

    std::string url = GenerateMSIUrl(msiEndpoint, identityClientId);

    int attempt = 1;
    TokenInfo tokenInfo;

    while (attempt <= MAX_RETRIES)
    {
        LOG_INFO("Sending request to MSI endpoint (attempt {}): {}", attempt, url);
        HttpResponse response = PerformCurlRequestWithHeaders(url, msiSecret);

        if (response.curlCode == CURLE_OK && response.statusCode == 200)
        {
            // Try to parse and validate token
            try {
                Json::Value jsonResponse;
                Json::CharReaderBuilder readerBuilder;
                std::string errors;
                std::istringstream responseStream(response.responseData);
                if (!Json::parseFromStream(readerBuilder, responseStream, &jsonResponse, &errors))
                {
                    LOG_WARN("Failed to parse JSON response: {}", errors);
                    throw RemoteClientException("JsonError: Failed to parse JSON response");
                }

                if (!jsonResponse.isMember("access_token") || !jsonResponse.isMember("expires_on"))
                {
                    LOG_ERROR("Invalid token response: missing required fields");
                    throw RemoteClientException("TokenFetchError: Invalid token response: missing required fields");
                }

                std::string accessToken = jsonResponse["access_token"].asString();
                int64_t expiresOn = std::stoll(jsonResponse["expires_on"].asString());
                auto expiryTime = std::chrono::system_clock::time_point(std::chrono::seconds(expiresOn));
                
                tokenInfo = { accessToken, expiryTime };
                LOG_INFO("Successfully fetched new access token for mountPath: {}", mountPath);
                return tokenInfo;  // Success case - immediately return

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to process token response: {}", e.what());
                throw RemoteClientException("TokenFetchError: Failed to process token response: " + std::string(e.what()));
            }
        }

        if (!IsRetryableError(response.curlCode, response.statusCode))
        {
            if (response.curlCode == CURLE_OK) {
                LOG_ERROR("Received non-retryable HTTP status code: {}", response.statusCode);
                throw RemoteClientException("TokenFetchError: Unexpected HTTP status code: " + std::to_string(response.statusCode));
            } else {
                LOG_ERROR("Non-retryable curl error: {} ({})", curl_easy_strerror(response.curlCode), response.curlCode);
                throw RemoteClientException("CurlError: " + std::string(curl_easy_strerror(response.curlCode)));
            }
        }

        attempt++;       
        if (attempt > MAX_RETRIES)
        {
            LOG_ERROR("Max retries {} reached for fetching token from MSI endpoint: {}", MAX_RETRIES, msiEndpoint);
            break;
        }

        // Exponential backoff with jitter
        LOG_WARN("Token fetch attempt {} failed, retrying...", attempt-1);
        auto interval_without_jitter = BASE_DELAY_SEC << attempt;
        std::uniform_int_distribution<int> dist(1, std::min(MAX_DELAY_SEC, interval_without_jitter));
        auto interval_with_jitter = std::chrono::seconds(dist(m_rng));
        LOG_INFO("Will retry in {} seconds", interval_with_jitter.count());
        std::this_thread::sleep_for(interval_with_jitter);
    }

    throw RemoteClientException("TokenFetchError: Max retries reached for fetching token from MSI endpoint");
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

    LOG_INFO("CURL response code: {}, http status code: {}", response.curlCode, response.statusCode);
    return response;
}
//---------------------------------------------------------------------------------------------------------------------
// Function to check if the error is retryable
bool BlobAuthTokenProvider::IsRetryableError(CURLcode curlCode, long statusCode)
{
    if (curlCode == CURLE_OK)
    {
        // Check HTTP status codes
        return (statusCode >= 500 || statusCode == 429);
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
