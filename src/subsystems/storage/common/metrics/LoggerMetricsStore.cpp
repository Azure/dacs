///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LoggerMetricsStore.h"
#include "MetricsLogger.h"
//---------------------------------------------------------------------------------------------------------------------
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{

//---------------------------------------------------------------------------------------------------------------------
LoggerMetricsStore::LoggerMetricsStore(std::chrono::seconds flushInterval)
    : m_flushInterval(flushInterval)
{
    m_flushThread = std::thread(&LoggerMetricsStore::FlushLoop, this);
}

//---------------------------------------------------------------------------------------------------------------------
LoggerMetricsStore::~LoggerMetricsStore()
{
    Shutdown();
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::IncrementCounter(std::string_view metricName,
                                          uint64_t         value,
                                          const Labels&    labels)
{
    const auto def = GetOrRegisterMetric(metricName);

    std::lock_guard<std::mutex> lock(m_counterMutex);
    CounterEntry& entry = m_counters[def.name];
    entry.total += value;
    entry.labels = def.autoLabels;
    if (!labels.empty()) entry.totals[labels] += value;
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::SetGauge(std::string_view metricName,
                                  double           value,
                                  const Labels&)
{
    // Note: Ideally we should track gauge values per label combination,
    //       but for simplicity and less logs, we ignore any non-default labels and only track the overall value.
    const auto def = GetOrRegisterMetric(metricName);

    std::lock_guard<std::mutex> lock(m_gaugeMutex);
    GaugeEntry& entry = m_gauges[def.name];
    entry.current = value;
    if (value < entry.min) entry.min = value;
    if (value > entry.max) entry.max = value;
    entry.labels = def.autoLabels;
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::ObserveHistogram(std::string_view metricName,
                                          double           value,
                                          const Labels&)
{
    // Note: Ideally we should track histogram values per label combination,
    //       but for simplicity and less logs, we ignore any non-default labels and only track the overall distribution.
    const auto def = GetOrRegisterMetric(metricName);

    std::lock_guard<std::mutex> lock(m_histogramMutex);
    HistogramEntry& entry = m_histograms[def.name];
    entry.count++;
    if (value < entry.min) entry.min = value;
    if (value > entry.max) entry.max = value;
    entry.labels = def.autoLabels;
    if (entry.samples.size() < MAX_HISTOGRAM_SAMPLES)
    {
        entry.samples.push_back(value);
    }
    else
    {
        // Reservoir sampling: replace an existing sample with decreasing probability as more observations are made.
        std::uniform_int_distribution<int64_t> distribution{0, entry.count - 1};
        const int64_t sampleIndex = distribution(m_randomGenerator);
        if (sampleIndex < static_cast<int64_t>(MAX_HISTOGRAM_SAMPLES))
        {
            entry.samples[static_cast<size_t>(sampleIndex)] = value;
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::Flush()
{
    DoFlush();
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::Shutdown()
{
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false))
    {
        m_flushCondition.notify_one();
        m_flushThread.join();
        DoFlush();
    }
}

//---------------------------------------------------------------------------------------------------------------------
// Private helpers
//---------------------------------------------------------------------------------------------------------------------

std::string LoggerMetricsStore::LabelsToString(const Labels& labels)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto& [k, v] : labels)
    {
        if (!first) oss << ',';
        oss << k << '=' << v;
        first = false;
    }
    return oss.str();
}

//---------------------------------------------------------------------------------------------------------------------
std::string LoggerMetricsStore::LabelTotalToString(const Labels& labels, uint64_t total)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : labels)
    {
        if (!first) oss << ',';
        oss << key << ':' << value;
        first = false;
    }
    oss << '=' << total;
    return oss.str();
}

//---------------------------------------------------------------------------------------------------------------------
double LoggerMetricsStore::GetPercentile(const std::vector<double>& sortedSamples,
                                                 double                     percentile)
{
    const auto rank = static_cast<size_t>(std::ceil(percentile * static_cast<double>(sortedSamples.size())));
    return sortedSamples[std::max<size_t>(rank, 1) - 1];
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::DoFlush()
{
    const auto periodEnd = std::chrono::system_clock::now();

    // --- Counters ---
    std::map<std::string, CounterEntry> counters;
    std::chrono::system_clock::time_point counterPeriodStart;
    {
        std::lock_guard<std::mutex> lock(m_counterMutex);
        counters.swap(m_counters);
        counterPeriodStart = m_counterPeriodStart;
        m_counterPeriodStart = periodEnd;
    }
    const auto counterStartTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        counterPeriodStart.time_since_epoch()).count();
    const auto periodEndTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        periodEnd.time_since_epoch()).count();
    for (const auto& [name, entry] : counters)
    {
        std::ostringstream labelTotals;
        for (const auto& [labels, total] : entry.totals)
        {
            labelTotals << ' ' << LabelTotalToString(labels, total);
        }
        LOG_INFO("type=counter name={} value={} {}{} period_start_ms={} period_end_ms={}",
             name, entry.total, LabelsToString(entry.labels), labelTotals.str(),
             counterStartTimeMs, periodEndTimeMs);
    }

    // --- Gauges ---
    std::map<std::string, GaugeEntry> gauges;
    std::chrono::system_clock::time_point gaugePeriodStart;
    {
        std::lock_guard<std::mutex> lock(m_gaugeMutex);
        gauges.swap(m_gauges);
        gaugePeriodStart = m_gaugePeriodStart;
        m_gaugePeriodStart = periodEnd;
    }
    const auto gaugeStartTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        gaugePeriodStart.time_since_epoch()).count();
    for (const auto& [name, entry] : gauges)
    {
           LOG_INFO("type=gauge name={} value={} min={} max={} {} period_start_ms={} period_end_ms={}",
               name, entry.current, entry.min, entry.max, LabelsToString(entry.labels),
               gaugeStartTimeMs, periodEndTimeMs);
    }

    // --- Histograms ---
    std::map<std::string, HistogramEntry> histograms;
    std::chrono::system_clock::time_point periodStart;
    {
        std::lock_guard<std::mutex> lock(m_histogramMutex);
        histograms.swap(m_histograms);
        periodStart = m_histogramPeriodStart;
        m_histogramPeriodStart = periodEnd;
    }

    const auto startTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        periodStart.time_since_epoch()).count();
    for (auto& [name, entry] : histograms)
    {
        if (entry.count == 0) continue;
        std::sort(entry.samples.begin(), entry.samples.end());
        const double avg = std::accumulate(entry.samples.begin(), entry.samples.end(), 0.0) /
                           static_cast<double>(entry.samples.size());
        const double squaredDeviationSum = std::accumulate(
            entry.samples.begin(), entry.samples.end(), 0.0,
            [avg](double sum, double sample) { return sum + (sample - avg) * (sample - avg); });
        const double standardDeviation = std::sqrt(
            squaredDeviationSum / static_cast<double>(entry.samples.size()));
        LOG_INFO("type=histogram name={} count={} min={} max={} avg={} stddev={} p50={} p95={} p99={} {} "
                    "period_start_ms={} period_end_ms={}",
                name,
                entry.count,
                fmt::format("{:.2f}", entry.min),
                fmt::format("{:.2f}", entry.max),
                fmt::format("{:.2f}", avg),
                fmt::format("{:.2f}", standardDeviation),
                fmt::format("{:.2f}", GetPercentile(entry.samples, 0.50)),
                fmt::format("{:.2f}", GetPercentile(entry.samples, 0.95)),
                fmt::format("{:.2f}", GetPercentile(entry.samples, 0.99)),
                LabelsToString(entry.labels),
                startTimeMs,
                periodEndTimeMs);
    }
}

//---------------------------------------------------------------------------------------------------------------------
void LoggerMetricsStore::FlushLoop()
{
    std::unique_lock<std::mutex> lock(m_flushMutex);
    while (m_running.load())
    {
        const bool shuttingDown = m_flushCondition.wait_for(
            lock, m_flushInterval, [this]() { return !m_running.load(); });
        if (!shuttingDown)
        {
            lock.unlock();
            DoFlush();
            lock.lock();
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------
