///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "MetricsStoreBase.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    // Aggregates metric observations in memory and periodically flushes a summary to spdlog.
    //
    // Per flush interval, logs:
    //   Counters   – total recorded during the period.
    //   Gauges     – last value recorded during the period.
    //   Histograms – per-period count, min, max, sampled average, and percentiles.
    //
    // Only dependency is spdlog, which is already project-wide.
    class LoggerMetricsStore final : public MetricsStoreBase
    {
    public:
        // flushInterval – how often aggregated stats are written to the log.
        explicit LoggerMetricsStore(
            std::chrono::seconds flushInterval = std::chrono::seconds{60});

        ~LoggerMetricsStore() override;

        void IncrementCounter(std::string_view metricName,
                      uint64_t         value,
                      const Labels&    labels) override;

        void SetGauge(std::string_view metricName,
                  double           value,
                  const Labels&    labels) override;

        void ObserveHistogram(std::string_view metricName,
                      double           value,
                      const Labels&    labels) override;

        // Force an immediate flush; does not reset the flush timer.
        void Flush() override;

        // Stop the background flush thread and emit a final flush.
        void Shutdown() override;

    private:
        static constexpr size_t MAX_HISTOGRAM_SAMPLES = 1000;

        struct CounterEntry
        {
            uint64_t total = 0;
            Labels labels; // Default labels
            std::map<Labels, uint64_t> totals; // Counts of non-default labels
        };

        struct GaugeEntry
        {
            double current = 0.0;
            double min     = std::numeric_limits<double>::max();
            double max     = std::numeric_limits<double>::lowest();
            Labels labels;
        };

        struct HistogramEntry
        {
            int64_t count = 0;
            double  min   = std::numeric_limits<double>::max();
            double  max   = std::numeric_limits<double>::lowest();
            Labels  labels;
            std::vector<double> samples;
        };
        // Helpers ------------------------------------------------------------

        // Serialises a Labels map to "k1=v1,k2=v2" for log output.
        static std::string LabelsToString(const Labels& labels);
        static std::string LabelTotalToString(const Labels& labels, uint64_t total);
        static double GetPercentile(const std::vector<double>& sortedSamples, double percentile);

        void DoFlush();
        void FlushLoop();

        // State --------------------------------------------------------------

        mutable std::mutex m_counterMutex;
        mutable std::mutex m_gaugeMutex;
        mutable std::mutex m_histogramMutex;

        // Intentionally using maps, so that metrics are logged in sorted order by their names.
        std::map<std::string, CounterEntry>   m_counters;
        std::map<std::string, GaugeEntry>     m_gauges;
        std::map<std::string, HistogramEntry> m_histograms;

        std::chrono::seconds m_flushInterval;
        std::chrono::system_clock::time_point m_counterPeriodStart{std::chrono::system_clock::now()};
        std::chrono::system_clock::time_point m_gaugePeriodStart{std::chrono::system_clock::now()};
        std::chrono::system_clock::time_point m_histogramPeriodStart{std::chrono::system_clock::now()};
        std::mt19937_64   m_randomGenerator{std::random_device{}()};
        std::atomic<bool> m_running{true};
        std::mutex        m_flushMutex;
        std::condition_variable m_flushCondition;
        std::thread       m_flushThread;
    };
}
//---------------------------------------------------------------------------------------------------------------------
