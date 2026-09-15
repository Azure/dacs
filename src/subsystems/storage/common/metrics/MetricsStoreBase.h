///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IMetricsStore.h"
#include <map>
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    class MetricsStoreBase : public IMetricsStore
    {
    public:
        MetricsStoreBase();

        void RegisterMetric(std::string_view name,
                            std::string_view description,
                            const Labels&    defaultLabels = {}) override;

    protected:
        MetricDef GetOrRegisterMetric(std::string_view name);

        static Labels MergeLabels(const MetricDef& def, const Labels& callLabels);

        int Rank() const;

    private:
        int                                        m_rank{-1};
        mutable std::mutex                         m_metricsMutex;
        std::map<std::string, MetricDef, std::less<>> m_metrics;
    };
}
//---------------------------------------------------------------------------------------------------------------------