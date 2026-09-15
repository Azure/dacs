///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Pushes metrics to an OpenTelemetry Collector via OTLP gRPC.
//
// Built into StorageMetrics and statically linked with opentelemetry-cpp.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "MetricsStoreBase.h"
#include <chrono>
#include <memory>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    class OtelMetricsStore final : public MetricsStoreBase
    {
    public:
        // endpoint       – OTLP HTTP metrics endpoint, e.g. "http://otel-collector:4318/v1/metrics"
        // serviceName    – OTel resource attribute "service.name".
        // exportInterval – how often the SDK pushes to the collector.
        explicit OtelMetricsStore(
            const std::string&        endpoint       = "http://localhost:4318/v1/metrics",
            const std::string&        serviceName    = "dacs",
            std::chrono::milliseconds exportInterval = std::chrono::milliseconds{15000});

        ~OtelMetricsStore() override;

        void IncrementCounter(std::string_view metricName,
                      uint64_t         value,
                      const Labels&    labels) override;

        void SetGauge(std::string_view metricName,
                  double           value,
                  const Labels&    labels) override;

        void ObserveHistogram(std::string_view metricName,
                      double           value,
                      const Labels&    labels) override;

        void Flush() override;
        void Shutdown() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
//---------------------------------------------------------------------------------------------------------------------
