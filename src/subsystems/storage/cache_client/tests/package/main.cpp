#include <CacheClient.h>
#include <LocatorCache.h>
#include <cache.pb.h>

int main()
{
    cache::LocatorLookupRequest lookup_request;
    lookup_request.set_key("package-smoke-test");

    kvclient::LocatorCache locator;
    locator.Init(0, 1);
    if (lookup_request.key().empty() || locator.GetNodeId() != 0)
    {
        return 1;
    }

    CacheClientRetryConfig retry_config{};
    retry_config.nMaxAttempts = 1;
    retry_config.nBaseDelayMillis = 1;
    retry_config.nMaxDelayMillis = 1;

    const CacheClientStatus status = Initialize(
        nullptr,
        0,
        0,
        false,
        false,
        false,
        nullptr,
        nullptr,
        false,
        retry_config);
    return status == INVALID_ARGUMENT ? 0 : 1;
}
