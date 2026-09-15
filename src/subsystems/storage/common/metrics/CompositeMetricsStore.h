///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IMetricsStore.h"
#include <memory>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    // Fans out every metric operation to multiple IMetricsStore backends simultaneously.
    //
    // Example – emit to both Prometheus and a periodic logger:
    //
    //   auto store = std::make_unique<CompositeMetricsStore>(
    //       std::vector<std::shared_ptr<IMetricsStore>>{
    //           std::make_shared<PrometheusMetricsStore>("0.0.0.0:8080", rank),
    //           std::make_shared<LoggerMetricsStore>(rank)});
    class CompositeMetricsStore final : public IMetricsStore
    {
    public:
        explicit CompositeMetricsStore(std::vector<std::shared_ptr<IMetricsStore>> stores);

        void RegisterMetric(std::string_view name,
                    std::string_view description,
                    const Labels&    defaultLabels = {}) override;

        void IncrementCounter(std::string_view metricName,
                      uint64_t         value,
                      const Labels&    labels) override;

        void SetGauge(std::string_view metricName,
                  double           value,
                  const Labels&    labels) override;

        void ObserveHistogram(std::string_view metricName,
                      double           value,
                      const Labels&    labels) override;

        void Flush()    override;
        void Shutdown() override;

    private:
        std::vector<std::shared_ptr<IMetricsStore>> m_stores;
    };
}
//---------------------------------------------------------------------------------------------------------------------
