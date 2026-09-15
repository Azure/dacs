///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CompositeMetricsStore.h"
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{

//---------------------------------------------------------------------------------------------------------------------
CompositeMetricsStore::CompositeMetricsStore(
    std::vector<std::shared_ptr<IMetricsStore>> stores)
    : m_stores(std::move(stores))
{
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::RegisterMetric(std::string_view name,
                                           std::string_view description,
                                           const Labels&    defaultLabels)
{
    for (auto& store : m_stores)
    {
        store->RegisterMetric(name, description, defaultLabels);
    }
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::IncrementCounter(std::string_view metricName,
                                              uint64_t         value,
                                              const Labels&    labels)
{
    for (auto& store : m_stores) store->IncrementCounter(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::SetGauge(std::string_view metricName,
                                     double           value,
                                     const Labels&    labels)
{
    for (auto& store : m_stores) store->SetGauge(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::ObserveHistogram(std::string_view metricName,
                                              double           value,
                                              const Labels&    labels)
{
    for (auto& store : m_stores) store->ObserveHistogram(metricName, value, labels);
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::Flush()
{
    for (auto& store : m_stores) store->Flush();
}

//---------------------------------------------------------------------------------------------------------------------
void CompositeMetricsStore::Shutdown()
{
    for (auto& store : m_stores) store->Shutdown();
}

//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------
