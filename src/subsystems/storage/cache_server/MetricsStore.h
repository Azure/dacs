#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include "cache.pb.h"

using namespace cache;

class MetricsStore
{
  public:
    std::shared_ptr<prometheus::Exposer> m_pExposer;
    std::shared_ptr<prometheus::Registry> m_spRegistry;
    std::string m_serverHostName;
    std::string m_sExposerAddress;
    prometheus::Gauge* m_pActiveConnectionsGauge;
    prometheus::Gauge* m_pCacheSizeGauge;
    prometheus::Counter* m_pCacheHitCounter;
    std::vector<prometheus::Histogram*> m_vecSuccessfulRequestLatencies;
    std::vector<prometheus::Histogram*> m_vecFailedRequestLatencies;
    constexpr static  std::array<const char*, (int)Request::TypeCase::kEndmarker> m_arrRequestTypes = {
        "Unknown",
        "Lock",
        "Upload",
        "Download",
        "Getattribute",
        "Putattribute",
        "Delete",
        "Ibinit",   
    };

    static MetricsStore s_instance;
};