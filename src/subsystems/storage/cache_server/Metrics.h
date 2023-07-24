#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"
#include "cache.pb.h"

using namespace cache;   
class Metrics {
    
    public:

        static void Init();
        static void IncrementActiveConnections();
        static void DecrementActiveConnections();
        static void RegisterRequestLatency(enum Request::TypeCase requestType, bool bIsSuccess, double nLatency);
        static void IncrementCacheHit();
        static void UpdateCacheSize(std::size_t nBytes);

};