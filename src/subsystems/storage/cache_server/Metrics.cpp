#include "Metrics.h"
#include "Utils.h"
#include <iostream>
#include "Logger.h"
#include "MetricsStore.h"

using namespace std;

//---------------------------------------------------------------------------------------------------------------------
MetricsStore MetricsStore::s_instance;
//---------------------------------------------------------------------------------------------------------------------
void Metrics::Init()
{    
    static_assert(MetricsStore::m_arrRequestTypes.size() == Request::TypeCase::kEndmarker,
              "Request::TypeCase and MetricsStore::m_vecRequestTypeToString are out of sync");
    
    LOG_INFO("Initializing cache server metrics");
    MetricsStore::s_instance.m_spRegistry = std::make_shared<prometheus::Registry>();
    MetricsStore::s_instance.m_sExposerAddress = "0.0.0.0:" + GetCacheServerMetricPort();
    MetricsStore::s_instance.m_pExposer = std::make_shared<prometheus::Exposer>(MetricsStore::s_instance.m_sExposerAddress);
    MetricsStore::s_instance.m_pExposer->RegisterCollectable(MetricsStore::s_instance.m_spRegistry);
    MetricsStore::s_instance.m_serverHostName = GetServerHostName();
    
    MetricsStore::s_instance.m_pActiveConnectionsGauge = &prometheus::BuildGauge()
            .Name("cache_server_active_connections")
            .Help("Number of active connections.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
    MetricsStore::s_instance.m_pCacheSizeGauge = &prometheus::BuildGauge()
            .Name("cache_server_cache_size_bytes")
            .Help("Size of the cache in bytes.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
    MetricsStore::s_instance.m_pCacheHitCounter = &prometheus::BuildCounter()
            .Name("cache_server_cache_hits")
            .Help("Number of cache hits.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"request_type", MetricsStore::m_arrRequestTypes[Request::TypeCase::kDownloadrequest]}});

    int nSize = MetricsStore::m_arrRequestTypes.size();
    MetricsStore::s_instance.m_vecSuccessfulRequestLatencies.resize(nSize);
    MetricsStore::s_instance.m_vecFailedRequestLatencies.resize(nSize);
    for (int i = 0; i < nSize; i++)
    {
        //creates a histogram with the following bucket intervals 0-5.0, 0.5-10.0 ...., 5000.0-10000.0, 10000.0-inf
        prometheus::Histogram::BucketBoundaries buckets = {5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0};
        std::string str = MetricsStore::m_arrRequestTypes[i];
        int index = i;
        MetricsStore::s_instance.m_vecSuccessfulRequestLatencies[index] = &prometheus::BuildHistogram()
                .Name("cache_server_successful_request_latency_ms")
                .Help("Latency of successful requests.")
                .Register(*MetricsStore::s_instance.m_spRegistry)
                .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"request_type", str}}, buckets);
        MetricsStore::s_instance.m_vecFailedRequestLatencies[index] = &prometheus::BuildHistogram()
                .Name("cache_server_failed_request_latency_ms")
                .Help("Latency of failed requests.")
                .Register(*MetricsStore::s_instance.m_spRegistry)
                .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"request_type", str}}, buckets);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementActiveConnections()
{
    MetricsStore::s_instance.m_pActiveConnectionsGauge->Increment();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::DecrementActiveConnections()
{
    MetricsStore::s_instance.m_pActiveConnectionsGauge->Decrement();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::RegisterRequestLatency(enum Request::TypeCase requestType, bool bIsSuccess, double latency)
{
    if (bIsSuccess)
    {
        MetricsStore::s_instance.m_vecSuccessfulRequestLatencies[(int)requestType]->Observe(latency);
    }
    else
    {
        MetricsStore::s_instance.m_vecFailedRequestLatencies[(int)requestType]->Observe(latency);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementCacheHit()
{
    MetricsStore::s_instance.m_pCacheHitCounter->Increment();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::UpdateCacheSize(std::size_t nBytes)
{
    MetricsStore::s_instance.m_pCacheSizeGauge->Set(nBytes);
}
//---------------------------------------------------------------------------------------------------------------------