///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "MetricDef.h"
#include "Labels.h"
#include <string_view>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    // Pure interface for a metrics backend.
    //
    // Available implementations:
    //   LoggerMetricsStore         – aggregates and logs via LOG_INFO on a timer (no extra deps).
    //   OtelMetricsStore           – pushes via OTLP HTTP; statically links opentelemetry-cpp at build time
    //   CompositeMetricsStore      – fans out to multiple backends simultaneously.
    //
    // To add a Prometheus /metrics scrape endpoint, implement a PrometheusMetricsStore.
    class IMetricsStore
    {
    public:
        virtual ~IMetricsStore() = default;

        virtual void RegisterMetric(std::string_view name,
                        std::string_view description,
                                    const Labels&      defaultLabels = {}) = 0;

        // Increment a monotonically increasing counter.
        virtual void IncrementCounter(std::string_view metricName,
                          uint64_t         value  = 1,
                          const Labels&    labels = {}) = 0;

        // Set a gauge to an arbitrary value (can increase or decrease).
        virtual void SetGauge(std::string_view metricName,
                      double           value,
                      const Labels&    labels = {}) = 0;

        // Record a single histogram observation (latency, size, etc.).
        virtual void ObserveHistogram(std::string_view metricName,
                          double           value,
                          const Labels&    labels = {}) = 0;

        // Flush any internally buffered data.  No-op for pull-based stores.
        virtual void Flush() {}

        // Graceful shutdown; waits for any in-flight exports to complete.
        virtual void Shutdown() {}
    };
}
//---------------------------------------------------------------------------------------------------------------------
