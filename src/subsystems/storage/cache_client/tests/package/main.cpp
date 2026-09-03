#include <CacheClient.h>

int main()
{
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
