///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageMetrics.h"
#include "CompositeMetricsStore.h"
#include "OtelMetricsStore.h"
#include "LoggerMetricsStore.h"
#include "MetricsLogger.h"
//---------------------------------------------------------------------------------------------------------------------
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string_view>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
namespace
{
    constexpr std::string_view DEFAULT_OTEL_ENDPOINT{"http://localhost:4318/v1/metrics"};
    constexpr std::string_view DEFAULT_OTEL_SERVICE_NAME{"dacs"};

    std::string_view GetIoSizeBucket(uint64_t ioSizeBytes)
    {
        if (ioSizeBytes <= 40 * 1024) return "40KB";
        if (ioSizeBytes <= 400 * 1024) return "400KB";
        if (ioSizeBytes <= 4 * 1024 * 1024) return "4MB";
        if (ioSizeBytes <= 40 * 1024 * 1024) return "40MB";
        return "huge";
    }

    struct MetricsState
    {
        std::mutex                                mutex;
        std::shared_ptr<CompositeMetricsStore>    store;
    };

    MetricsState& GetState()
    {
        static MetricsState state;
        return state;
    }

    void PrepareForFork()
    {
        MetricsState& state = GetState();
        state.mutex.lock();
        if (state.store)
        {
            state.store->Shutdown();
            state.store.reset();
        }
    }

    void AfterFork()
    {
        GetState().mutex.unlock();
    }

    bool GetBooleanEnvironmentVariable(const char* name, bool defaultValue)
    {
        const char* rawValue = std::getenv(name);
        if (rawValue == nullptr) return defaultValue;

        const std::string_view value{rawValue};
        if (value == "true" || value == "TRUE" || value == "1") return true;
        if (value == "false" || value == "FALSE" || value == "0") return false;
        return defaultValue;
    }

    std::chrono::minutes GetIntervalEnvironmentVariable(const char* name, int64_t defaultValue)
    {
        const char* rawValue = std::getenv(name);
        if (rawValue == nullptr) return std::chrono::minutes{defaultValue};

        int64_t value = 0;
        const std::string_view text{rawValue};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value <= 0)
        {
            return std::chrono::minutes{defaultValue};
        }
        return std::chrono::minutes{value};
    }

    std::string GetStringEnvironmentVariable(const char* name, std::string_view defaultValue)
    {
        const char* value = std::getenv(name);
        return value == nullptr || *value == '\0' ? std::string{defaultValue} : std::string{value};
    }
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::Initialize()
{
    MetricsState& state = GetState();
    static std::once_flag handlerRegistration;
    std::call_once(handlerRegistration, []()
    {
        pthread_atfork(PrepareForFork, AfterFork, AfterFork);
        std::atexit([]() { StorageMetrics::Shutdown(); });
    });

    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) return;

    std::vector<std::shared_ptr<IMetricsStore>> stores;
    if (GetBooleanEnvironmentVariable("STORAGE_METRIC_LOGGING_ENABLED", true))
    {
        try
        {
            stores.push_back(std::make_shared<LoggerMetricsStore>(
                GetIntervalEnvironmentVariable("STORAGE_METRIC_LOGGING_INTERVAL_MINS", 15)));
        }
        catch (const std::exception& ex)
        {
            LOG_WARN("StorageMetrics: Failed to initialize logger metrics store: {}", ex.what());
        }
        catch (...)
        {
            LOG_WARN("StorageMetrics: Failed to initialize logger metrics store");
        }
    }

    if (GetBooleanEnvironmentVariable("STORAGE_METRIC_OTEL_ENABLED", false))
    {
        try
        {
            stores.push_back(std::make_shared<OtelMetricsStore>(
                GetStringEnvironmentVariable("STORAGE_METRIC_OTEL_ENDPOINT", DEFAULT_OTEL_ENDPOINT),
                GetStringEnvironmentVariable("STORAGE_METRIC_OTEL_SERVICENAME", DEFAULT_OTEL_SERVICE_NAME),
                GetIntervalEnvironmentVariable("STORAGE_METRIC_OTEL_EXPORT_INTERVAL_MINS", 5)));
        }
        catch (const std::exception& ex)
        {
            LOG_WARN("StorageMetrics: Failed to initialize OpenTelemetry metrics store: {}", ex.what());
        }
        catch (...)
        {
            LOG_WARN("StorageMetrics: Failed to initialize OpenTelemetry metrics store");
        }
    }

    if (!stores.empty())
    {
        state.store = std::make_shared<CompositeMetricsStore>(std::move(stores));
    }
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::Flush()
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) state.store->Flush();
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::Shutdown()
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.store) return;

    state.store->Shutdown();
    state.store.reset();
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::RegisterMetric(std::string_view name,
                                    std::string_view description,
                                    const Labels&    defaultLabels)
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) state.store->RegisterMetric(name, description, defaultLabels);
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::IncrementCounter(std::string_view metricName,
                                      uint64_t         value,
                                      const Labels&    labels)
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) state.store->IncrementCounter(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::SetGauge(std::string_view metricName,
                              double           value,
                              const Labels&    labels)
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) state.store->SetGauge(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::ObserveHistogram(std::string_view metricName,
                                      double           value,
                                      const Labels&    labels)
{
    MetricsState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.store) state.store->ObserveHistogram(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void StorageMetrics::RecordLatency(std::string_view metricName,
                                   uint64_t         timeMillis,
                                   uint64_t         ioSizeBytes,
                                   const Labels&    labels)
{
    const std::string ioSizeBucket{GetIoSizeBucket(ioSizeBytes)};

    // Record overall latency measurement
    ObserveHistogram(metricName, static_cast<double>(timeMillis), labels);

    // Record latency measurement for the specific IO size bucket
    // Note: we could have added the IO size bucket as a label in overall histogram,
    //       but this gives distinct metric name for each bucket
    ObserveHistogram(
        std::string{metricName} + "_" + ioSizeBucket,
        static_cast<double>(timeMillis),
        labels);
}

//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------