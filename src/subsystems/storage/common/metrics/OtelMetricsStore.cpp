///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "OtelMetricsStore.h"
#include "MetricsLogger.h"
//---------------------------------------------------------------------------------------------------------------------
#include <map>
#include <mutex>
#include <unordered_map>
//---------------------------------------------------------------------------------------------------------------------
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry_factory.h>
#include <opentelemetry/sdk/resource/resource.h>

namespace otlp     = opentelemetry::exporter::otlp;
namespace metrics  = opentelemetry::metrics;
namespace sdk      = opentelemetry::sdk::metrics;
namespace resource = opentelemetry::sdk::resource;
namespace nostd    = opentelemetry::nostd;
namespace common   = opentelemetry::common;
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{

//---------------------------------------------------------------------------------------------------------------------
struct OtelMetricsStore::Impl
{
    struct GaugeState
    {
        nostd::shared_ptr<metrics::ObservableInstrument> instrument;
        std::mutex                                       mutex;
        std::map<Labels, double>                         values;
    };

    std::shared_ptr<sdk::MeterProvider>  sdkProvider;
    nostd::shared_ptr<metrics::Meter>    meter;
    std::chrono::milliseconds            flushTimeout{5000};

    std::mutex handleMutex;
    std::unordered_map<std::string, nostd::unique_ptr<metrics::Counter<uint64_t>>>     counters;
    std::unordered_map<std::string, std::unique_ptr<GaugeState>>                       gauges;
    std::unordered_map<std::string, nostd::unique_ptr<metrics::Histogram<double>>>     histograms;

    // Build an OTel attribute list from our Labels map.
    using OtelAttrs = std::vector<std::pair<nostd::string_view, common::AttributeValue>>;
    static OtelAttrs ToOtelAttrs(const Labels& labels)
    {
        OtelAttrs attrs;
        attrs.reserve(labels.size());
        for (const auto& [k, v] : labels)
            attrs.emplace_back(nostd::string_view{k}, nostd::string_view{v});
        return attrs;
    }

    static void ObserveGauge(metrics::ObserverResult result, void* state)
    {
        auto observer = nostd::get<nostd::shared_ptr<metrics::ObserverResultT<double>>>(result);
        auto* gaugeState = static_cast<GaugeState*>(state);

        std::lock_guard<std::mutex> lock(gaugeState->mutex);
        for (const auto& [labels, value] : gaugeState->values)
        {
            auto attrs = ToOtelAttrs(labels);
            observer->Observe(value, common::KeyValueIterableView<decltype(attrs)>{attrs});
        }
    }
};

//---------------------------------------------------------------------------------------------------------------------
OtelMetricsStore::OtelMetricsStore(const std::string&        endpoint,
                                   const std::string&        serviceName,
                                   std::chrono::milliseconds exportInterval)
    : m_impl(std::make_unique<Impl>())
{
    auto resAttrs = resource::ResourceAttributes{{"service.name", serviceName}};
    if (Rank() >= 0) resAttrs["process.rank"] = std::to_string(Rank());
    auto otelResource = resource::Resource::Create(resAttrs);

    otlp::OtlpHttpMetricExporterOptions expOpts;
    expOpts.url = endpoint;
    auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(expOpts);

    sdk::PeriodicExportingMetricReaderOptions readerOpts;
    readerOpts.export_interval_millis = exportInterval;
    readerOpts.export_timeout_millis  =
        std::chrono::milliseconds{std::min(exportInterval.count() / 2, int64_t{5000})};
    m_impl->flushTimeout = readerOpts.export_timeout_millis;
    auto reader = sdk::PeriodicExportingMetricReaderFactory::Create(
        std::move(exporter), readerOpts);

    m_impl->sdkProvider = sdk::MeterProviderFactory::Create(
        sdk::MeterContextFactory::Create(
            sdk::ViewRegistryFactory::Create(),
            otelResource));
    m_impl->sdkProvider->AddMetricReader(std::move(reader));
    m_impl->meter = m_impl->sdkProvider->GetMeter(serviceName, "1.0.0");

    LOG_INFO("OtelMetricsStore: Setup metrics push to {} service={} export_interval={}ms timeout={}ms",
             endpoint,
             serviceName,
             readerOpts.export_interval_millis.count(),
             readerOpts.export_timeout_millis.count());
}

//---------------------------------------------------------------------------------------------------------------------
OtelMetricsStore::~OtelMetricsStore()
{
    Shutdown();
}

//---------------------------------------------------------------------------------------------------------------------
void OtelMetricsStore::Flush()
{
    if (m_impl->sdkProvider && !m_impl->sdkProvider->ForceFlush(m_impl->flushTimeout))
    {
        LOG_WARN("OtelMetricsStore: ForceFlush did not complete within {}ms", m_impl->flushTimeout.count());
    }
}

//---------------------------------------------------------------------------------------------------------------------
void OtelMetricsStore::Shutdown()
{
    if (m_impl->sdkProvider)
    {
        if (!m_impl->sdkProvider->ForceFlush(m_impl->flushTimeout))
        {
            LOG_WARN("OtelMetricsStore: ForceFlush during shutdown did not complete within {}ms",
                     m_impl->flushTimeout.count());
        }

        std::lock_guard<std::mutex> lock(m_impl->handleMutex);
        for (auto& [name, gauge] : m_impl->gauges)
        {
            gauge->instrument->RemoveCallback(&Impl::ObserveGauge, gauge.get());
        }
        m_impl->gauges.clear();
        m_impl->sdkProvider.reset();
    }
}

//---------------------------------------------------------------------------------------------------------------------
void OtelMetricsStore::IncrementCounter(std::string_view metricName,
                                        uint64_t         value,
                                        const Labels&    labels)
{
    const auto def = GetOrRegisterMetric(metricName);
    const Labels merged = MergeLabels(def, labels);
    metrics::Counter<uint64_t>* counter = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_impl->handleMutex);
        auto it = m_impl->counters.find(def.name);
        if (it == m_impl->counters.end())
        {
            m_impl->counters[def.name] =
                m_impl->meter->CreateUInt64Counter(def.name, def.description);
            it = m_impl->counters.find(def.name);
        }
        counter = it->second.get();
    }
    auto attrs = Impl::ToOtelAttrs(merged);
    counter->Add(value,
                 common::KeyValueIterableView<decltype(attrs)>{attrs});
}

//---------------------------------------------------------------------------------------------------------------------
void OtelMetricsStore::SetGauge(std::string_view metricName,
                                double           value,
                                const Labels&    labels)
{
    const auto def = GetOrRegisterMetric(metricName);
    const Labels merged = MergeLabels(def, labels);
    Impl::GaugeState* gauge = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_impl->handleMutex);
        auto it = m_impl->gauges.find(def.name);
        if (it == m_impl->gauges.end())
        {
            auto state = std::make_unique<Impl::GaugeState>();
            state->instrument = m_impl->meter->CreateDoubleObservableGauge(def.name, def.description);
            state->instrument->AddCallback(&Impl::ObserveGauge, state.get());
            it = m_impl->gauges.emplace(def.name, std::move(state)).first;
        }
        gauge = it->second.get();
    }

    std::lock_guard<std::mutex> lock(gauge->mutex);
    gauge->values[merged] = value;
}

//---------------------------------------------------------------------------------------------------------------------
void OtelMetricsStore::ObserveHistogram(std::string_view metricName,
                                        double           value,
                                        const Labels&    labels)
{
    const auto def = GetOrRegisterMetric(metricName);
    const Labels merged = MergeLabels(def, labels);
    metrics::Histogram<double>* histogram = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_impl->handleMutex);
        auto it = m_impl->histograms.find(def.name);
        if (it == m_impl->histograms.end())
        {
            m_impl->histograms[def.name] =
                m_impl->meter->CreateDoubleHistogram(def.name, def.description);
            it = m_impl->histograms.find(def.name);
        }
        histogram = it->second.get();
    }
    auto attrs = Impl::ToOtelAttrs(merged);
    histogram->Record(value,
                      common::KeyValueIterableView<decltype(attrs)>{attrs},
                      opentelemetry::context::RuntimeContext::GetCurrent());
}

//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------
