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
              "Request::TypeCase and MetricsStore::m_arrRequestTypes are out of sync");
    static_assert(MetricsStore::m_arrResponseCodes.size() == (int)ResponseCode::END_MARKER,
              "ResponseCode and MetricsStore::m_arrResponseCodes are out of sync");
    
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

    int nSize = MetricsStore::m_arrRequestTypes.size();
    MetricsStore::s_instance.m_vecSuccessfulRequestLatencies.resize(nSize);

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
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"request_type", str}, {"status", MetricsStore::m_arrResponseCodes[(int)ResponseCode::SUCCESS]}}, buckets);

        int requestVecSize = MetricsStore::m_arrResponseCodes.size();
        MetricsStore::s_instance.m_vecRequestCounter.push_back(std::vector<prometheus::Counter*>(requestVecSize));
        for( int j = 0; j < requestVecSize; j++)
        {
            std::string errorCode = MetricsStore::m_arrResponseCodes[j];
            MetricsStore::s_instance.m_vecRequestCounter[index][j] = &prometheus::BuildCounter()
                    .Name("cache_server_request_counter")
                    .Help("Number of requests.")
                    .Register(*MetricsStore::s_instance.m_spRegistry)
                    .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"request_type", str}, {"status", errorCode}});
        }
    }

    prometheus::Histogram::BucketBoundaries sizeBuckets = {100000, 512000, 1024000, 4*1024000, 16*1024000};
    MetricsStore::s_instance.m_pDownloadRequestSizeHistogram = &prometheus::BuildHistogram()
            .Name("cache_server_download_request_size_bytes")
            .Help("Size of download requests.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}}, sizeBuckets);
    MetricsStore::s_instance.m_pUploadRequestSizeHistogram = &prometheus::BuildHistogram().
            Name("cache_server_upload_request_size_bytes")
            .Help("Size of upload requests.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}}, sizeBuckets);

    // TachyonKV locator cache metrics
    MetricsStore::s_instance.m_pLocatorLookupsHit = &prometheus::BuildCounter()
            .Name("tachyonkv_locator_lookups_total")
            .Help("Locator cache lookups.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"result", "hit"}});
    MetricsStore::s_instance.m_pLocatorLookupsMiss = &prometheus::BuildCounter()
            .Name("tachyonkv_locator_lookups_total")
            .Help("Locator cache lookups.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}, {"result", "miss"}});
    MetricsStore::s_instance.m_pLocatorRegistrations = &prometheus::BuildCounter()
            .Name("tachyonkv_locator_registrations_total")
            .Help("Locator cache registrations.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
    MetricsStore::s_instance.m_pTTLExtensions = &prometheus::BuildCounter()
            .Name("tachyonkv_ttl_extensions_total")
            .Help("TTL refresh operations.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
    MetricsStore::s_instance.m_pLocatorEntryCount = &prometheus::BuildGauge()
            .Name("tachyonkv_locator_entries")
            .Help("Number of locator cache entries.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
    MetricsStore::s_instance.m_pLocatorReplicaCount = &prometheus::BuildGauge()
            .Name("tachyonkv_locator_replicas")
            .Help("Number of locator cache replicas.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});

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
void Metrics::RegisterRequest(enum Request::TypeCase eRequestType,
                              enum ResponseCode eResponseCode,
                              double dLatency,
                              std::size_t nBytes)
{

    switch (eResponseCode)
    {
        case ResponseCode::SUCCESS:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::SUCCESS]->Increment();
            MetricsStore::s_instance.m_vecSuccessfulRequestLatencies[(int)eRequestType]->Observe(dLatency);
            if(eRequestType == Request::TypeCase::kDownloadrequest)
            {
                MetricsStore::s_instance.m_pDownloadRequestSizeHistogram->Observe(nBytes);
            }
            else if (eRequestType == Request::TypeCase::kUploadrequest)
            {
                MetricsStore::s_instance.m_pUploadRequestSizeHistogram->Observe(nBytes);
            }
            break;
        case ResponseCode::INVALID_TRANSITION:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::INVALID_TRANSITION]->Increment();
            break;
        case ResponseCode::BAD_REQUEST:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::BAD_REQUEST]->Increment();
            break;
        case ResponseCode::INTERNAL_ERROR:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::INTERNAL_ERROR]->Increment();
            break;
        case ResponseCode::NETWORK_ERROR:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::NETWORK_ERROR]->Increment();
            break;
        default:
            MetricsStore::s_instance.m_vecRequestCounter[(int)eRequestType][(int)ResponseCode::NOT_SET]->Increment();
            break;         
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::UpdateCacheSize(std::size_t nBytes)
{
    MetricsStore::s_instance.m_pCacheSizeGauge->Set(nBytes);
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementLocatorLookups(bool bHit)
{
    if (MetricsStore::s_instance.m_pLocatorLookupsHit == nullptr ||
        MetricsStore::s_instance.m_pLocatorLookupsMiss == nullptr)
    {
        return;
    }

    if (bHit)
        MetricsStore::s_instance.m_pLocatorLookupsHit->Increment();
    else
        MetricsStore::s_instance.m_pLocatorLookupsMiss->Increment();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementLocatorRegistrations()
{
    if (MetricsStore::s_instance.m_pLocatorRegistrations == nullptr)
    {
        return;
    }
    MetricsStore::s_instance.m_pLocatorRegistrations->Increment();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementTTLExtensions()
{
    if (MetricsStore::s_instance.m_pTTLExtensions == nullptr)
    {
        return;
    }
    MetricsStore::s_instance.m_pTTLExtensions->Increment();
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::SetLocatorEntryCount(std::size_t nCount)
{
    if (MetricsStore::s_instance.m_pLocatorEntryCount == nullptr)
    {
        return;
    }
    MetricsStore::s_instance.m_pLocatorEntryCount->Set(nCount);
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::SetLocatorReplicaCount(std::size_t nCount)
{
    if (MetricsStore::s_instance.m_pLocatorReplicaCount == nullptr)
    {
        return;
    }
    MetricsStore::s_instance.m_pLocatorReplicaCount->Set(nCount);
}
//---------------------------------------------------------------------------------------------------------------------