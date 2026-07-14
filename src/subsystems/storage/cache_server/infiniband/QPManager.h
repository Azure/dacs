///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IBConstants.h"
#include "IBManager.h"
#include "IBUtils.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
//---------------------------------------------------------------------------------------------------------------------
class QPManager
{
    public:
        QPManager(uint32_t maxQps);
        ~QPManager();

        // Create and initialize QP
        ibutils::shared_ibv_qp CreateQP(/*[in]*/ ibv_cq* sendCQ, /*[in]*/ ibv_srq* receiveSRQ, /*[in]*/ ibv_pd* pd);

        // Store the QP for reuse
        bool InsertQP(/*[in]*/ ibutils::shared_ibv_qp qp);

        // Find and fetch active QP 
        ibutils::shared_ibv_qp FindQP(/*[in]*/ uint32_t qpNum);

        // Remove and cleanup the QP
        bool DeleteQP(/*[in]*/ uint32_t qpNum);

    private:

        // Bucket ID for sharding and locking to avoid a global lock
        unsigned int GetBucket(/*[in ]*/ uint32_t qpNum);

        // Worker timer loop to cleanup stale QPs
        void QPCleanupWorker();

    private:

        // Map to store the QPs associated with clients, bucketized to avoid a single global lock.
        // We use shared_ptr for QP since it can be used in active connections even after its removed from this
        // data structure. The custom deleter will ensure the qp resource is released after the last reference.
        struct QPLRUInfo
        {
            QPLRUInfo(uint32_t qpNum, time_t lruTime)
                : qpNum(qpNum), lruTime(lruTime) {}
            const uint32_t qpNum;
            const time_t lruTime;
        };
        struct QPInfo
        {
            QPInfo(ibutils::shared_ibv_qp ibvQp, std::list<QPLRUInfo>::iterator lruIterator)
                : ibvQp(ibvQp), lruIterator(lruIterator) {}
            const ibutils::shared_ibv_qp ibvQp;
            std::list<QPLRUInfo>::iterator lruIterator;
        };
        std::vector<std::unordered_map<uint32_t, QPInfo>>                   m_vQpsMap;

        // LRU cache
        std::vector<std::list<QPLRUInfo>>                                   m_vQpsLruQueue;

        // Mutices for individual buckets to avoid a single lock
        std::vector<std::mutex>                                             m_vQpsMutex;

        // Track total number of active QPs
        std::atomic_uint_fast32_t                                           m_nActiveQps;

        // Background thread to cleanup stale/old QPs
        std::unique_ptr<std::thread>                                        m_qpCleanupThread;

        // Default LRU expiry time for QPs (5 minutes — reduced from 30 min to prevent
        // QP table exhaustion on high-churn GB300 clusters where peers die frequently)
        static constexpr uint32_t DEFAULT_QP_EXPIRY_SECONDS =               300;

        // Default percentage of max qp limit beyond which we need to do aggressive qp cleanup
        static constexpr uint32_t DEFAULT_QP_CRITICAL_LIMIT_PERCENT =       50;

        // Default wake up timer for QP cleanup thread
        static constexpr uint32_t DEFAULT_QP_CLEANUP_INTERVAL_SECONDS =     30;

        // QP expiry time. QPs whose LRU time is beyond this time will be destroyed asynchronously
        // TODO: Make this configurable
        const uint32_t                                                      m_qpInactiveExpirySeconds;

        // Cleanup thread time interval to check for stale QPs
        // TODO: Make this configurable
        const uint32_t                                                      m_cleanupIntervalSeconds;

        // Mutex and CV to wait/notify the cleanup thread. Used to immediately wake up the thread on shutdown
        std::mutex                                                          m_mutexCleanupThread;
        std::condition_variable                                             m_cvCleanupThread;

        // Flag to stop the cleanup thread
        std::atomic_bool                                                    m_stopCleanupThread;

        // Limit the number of open/active QPs in server to avoid hitting the system limit.
        // If QP count hits this value, we will aggressively delete QPs to prevent future QP creation failures.
        // TODO: Make this configurable
        const uint32_t                                                      m_criticalQpsLimit;
};
//---------------------------------------------------------------------------------------------------------------------