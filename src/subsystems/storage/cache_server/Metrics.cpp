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
#include <string>

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
        MetricsStore::s_instance.m_pListenBacklogMaxGauge = &prometheus::BuildGauge()
            .Name("cache_server_listen_backlog_max")
            .Help("Configured listen backlog passed to listen().")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pAcceptQueueOverflowsCounter = &prometheus::BuildCounter()
            .Name("cache_server_accept_queue_overflows_total")
            .Help("Total accept queue overflows from TcpExt.ListenOverflows.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pListenDropsCounter = &prometheus::BuildCounter()
            .Name("cache_server_listen_drops_total")
            .Help("Total listen drops from TcpExt.ListenDrops.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pSynQueueFullDropsCounter = &prometheus::BuildCounter()
            .Name("cache_server_syn_queue_full_drops_total")
            .Help("Total SYN request queue drops from TcpExt.TCPReqQFullDrop.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pSynCookiesSentCounter = &prometheus::BuildCounter()
            .Name("cache_server_syn_cookies_sent_total")
            .Help("Total SYN cookies sent from TcpExt.TCPReqQFullDoCookies.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pPassiveOpensCounter = &prometheus::BuildCounter()
            .Name("cache_server_passive_opens_total")
            .Help("Total PassiveOpens from Tcp.PassiveOpens via nstat.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pOpenFdCountGauge = &prometheus::BuildGauge()
            .Name("cache_server_open_fds")
            .Help("Current number of open file descriptors from /proc/sys/fs/file-nr.")
            .Register(*MetricsStore::s_instance.m_spRegistry)
            .Add({{"instance", MetricsStore::s_instance.m_serverHostName}});
        MetricsStore::s_instance.m_pAcceptErrorsFamily = &prometheus::BuildCounter()
            .Name("cache_server_accept_errors_total")
            .Help("Total accept() errors, labeled by errno value.")
            .Register(*MetricsStore::s_instance.m_spRegistry);

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
void Metrics::SetListenBacklogMax(std::size_t backlog)
{
    MetricsStore::s_instance.m_pListenBacklogMaxGauge->Set(backlog);
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementAcceptQueueOverflows(uint64_t delta)
{
    if (delta > 0)
    {
        MetricsStore::s_instance.m_pAcceptQueueOverflowsCounter->Increment(static_cast<double>(delta));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementListenDrops(uint64_t delta)
{
    if (delta > 0)
    {
        MetricsStore::s_instance.m_pListenDropsCounter->Increment(static_cast<double>(delta));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementSynQueueFullDrops(uint64_t delta)
{
    if (delta > 0)
    {
        MetricsStore::s_instance.m_pSynQueueFullDropsCounter->Increment(static_cast<double>(delta));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementSynCookiesSent(uint64_t delta)
{
    if (delta > 0)
    {
        MetricsStore::s_instance.m_pSynCookiesSentCounter->Increment(static_cast<double>(delta));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementPassiveOpens(uint64_t delta)
{
    if (delta > 0)
    {
        MetricsStore::s_instance.m_pPassiveOpensCounter->Increment(static_cast<double>(delta));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::UpdateOpenFdCount(std::size_t count)
{
    MetricsStore::s_instance.m_pOpenFdCountGauge->Set(count);
}
//---------------------------------------------------------------------------------------------------------------------
void Metrics::IncrementAcceptError(int errnum)
{
    auto it = MetricsStore::s_instance.m_mapAcceptErrorCounters.find(errnum);
    if (it == MetricsStore::s_instance.m_mapAcceptErrorCounters.end())
    {
        std::string errString = std::to_string(errnum);
        auto& counter = MetricsStore::s_instance.m_pAcceptErrorsFamily->Add(
            {{"instance", MetricsStore::s_instance.m_serverHostName}, {"errno", errString}});
        it = MetricsStore::s_instance.m_mapAcceptErrorCounters.emplace(errnum, &counter).first;
    }

    it->second->Increment();
}
//---------------------------------------------------------------------------------------------------------------------