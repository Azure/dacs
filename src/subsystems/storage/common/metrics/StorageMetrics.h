///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "Labels.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    // Singleton class for recording metrics. Use the provided macros for convenience.
    //
    // Notes:
    //  1. RegisterMetric is optional, metrics can be recorded without explicit registration.
    //  2. Metrics recording is a no-op unless Initialize() has been called.
    //  3. This class is fork-safe, Initialize() should be called after a fork in both parent and child processes.
    //  4. All methods are static and thread-safe.
    class StorageMetrics final
    {
    public:
        static void Initialize();
        static void Flush();
        static void Shutdown();

        static void RegisterMetric(std::string_view name,
                       std::string_view description,
                       const Labels&    defaultLabels = {});

        static void IncrementCounter(std::string_view metricName,
                         uint64_t         value  = 1,
                         const Labels&    labels = {});

        static void SetGauge(std::string_view metricName,
                     double           value,
                     const Labels&    labels = {});

        static void ObserveHistogram(std::string_view metricName,
                         double           value,
                         const Labels&    labels = {});

        // Figure out right histogram bucket based on IO size and update.
        static void RecordLatency(std::string_view metricName,
                      uint64_t         timeMillis,
                      uint64_t         ioSizeBytes,
                      const Labels&    labels = {});
    };
}
//---------------------------------------------------------------------------------------------------------------------
// Initializes metrics once from environment configuration.
#define STORAGE_METRIC_INITIALIZE() ::Dacs::Metrics::StorageMetrics::Initialize()
// Flushes initialized metrics; example: STORAGE_METRIC_FLUSH();
#define STORAGE_METRIC_FLUSH() ::Dacs::Metrics::StorageMetrics::Flush()
// Flushes and disables initialized metrics.
#define STORAGE_METRIC_SHUTDOWN() ::Dacs::Metrics::StorageMetrics::Shutdown()
// Registers a metric description and optional default labels.
// Example: STORAGE_METRIC_REGISTER("upload_requests", "Requests for uploads");
#define STORAGE_METRIC_REGISTER(...) ::Dacs::Metrics::StorageMetrics::RegisterMetric(__VA_ARGS__)
// Increments a counter by an optional value and labels.
// Example: STORAGE_METRIC_COUNTER_INCREMENT("upload_requests");
#define STORAGE_METRIC_COUNTER_INCREMENT(...) \
    ::Dacs::Metrics::StorageMetrics::IncrementCounter(__VA_ARGS__)
// Sets a gauge value with optional labels.
// Example: STORAGE_METRIC_SET_GAUGE("queue_depth", 12.0);
#define STORAGE_METRIC_SET_GAUGE(...) ::Dacs::Metrics::StorageMetrics::SetGauge(__VA_ARGS__)
// Records a histogram observation with optional labels.
// Example: STORAGE_METRIC_OBSERVE_HISTOGRAM("latency_ms", 4.2);
#define STORAGE_METRIC_OBSERVE_HISTOGRAM(...) \
    ::Dacs::Metrics::StorageMetrics::ObserveHistogram(__VA_ARGS__)
// Starts a scoped latency timer; example: STORAGE_METRIC_TIMER_START(cache_client_upload_latency_ms);
#define STORAGE_METRIC_TIMER_START(timerName) \
    const auto timerName##_startTime = std::chrono::steady_clock::now()
// Records elapsed milliseconds and I/O size; example: STORAGE_METRIC_TIMER_END(cache_client_upload_latency_ms, size);
#define STORAGE_METRIC_TIMER_END(timerName, ioSizeBytes, ...) do { \
    const auto timerName##_endTime = std::chrono::steady_clock::now(); \
    const auto timerName##_elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>( \
        timerName##_endTime - timerName##_startTime).count(); \
    ::Dacs::Metrics::StorageMetrics::RecordLatency( \
        #timerName, \
        static_cast<uint64_t>(timerName##_elapsedMillis), \
        ioSizeBytes __VA_OPT__(,) __VA_ARGS__); \
} while (false)
//---------------------------------------------------------------------------------------------------------------------