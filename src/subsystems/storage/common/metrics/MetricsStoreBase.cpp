///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "MetricsStoreBase.h"
//---------------------------------------------------------------------------------------------------------------------
#include <charconv>
#include <cstdlib>
#include <unistd.h>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{

//---------------------------------------------------------------------------------------------------------------------
MetricsStoreBase::MetricsStoreBase()
{
    const char* rankValue = std::getenv("RANK");
    if (rankValue == nullptr) return;

    int rank = -1;
    const std::string_view value{rankValue};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), rank);
    if (error == std::errc{} && end == value.data() + value.size() && rank >= 0) m_rank = rank;
}

//---------------------------------------------------------------------------------------------------------------------
void MetricsStoreBase::RegisterMetric(std::string_view name,
                                      std::string_view description,
                                      const Labels&    defaultLabels)
{
    Labels labels{{"pid", std::to_string(static_cast<int>(::getpid()))}};
    if (m_rank >= 0) labels["rank"] = std::to_string(m_rank);
    for (const auto& [key, value] : defaultLabels) labels[key] = value;

    std::lock_guard<std::mutex> lock(m_metricsMutex);
    std::string ownedName{name};
    m_metrics.try_emplace(ownedName, MetricDef{ownedName, std::string{description}, std::move(labels)});
}

//---------------------------------------------------------------------------------------------------------------------
MetricDef MetricsStoreBase::GetOrRegisterMetric(std::string_view name)
{
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        const auto it = m_metrics.find(name);
        if (it != m_metrics.end()) return it->second;
    }

    RegisterMetric(name, "");

    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_metrics.find(name)->second;
    }
}

//---------------------------------------------------------------------------------------------------------------------
Labels MetricsStoreBase::MergeLabels(const MetricDef& def, const Labels& callLabels)
{
    Labels merged = def.autoLabels;
    for (const auto& [key, value] : callLabels) merged[key] = value;
    return merged;
}

//---------------------------------------------------------------------------------------------------------------------
int MetricsStoreBase::Rank() const
{
    return m_rank;
}

//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------