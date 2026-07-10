///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "QPManager.h"
#include "IBManager.h"
#include "Constants.h"
#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace ibutils;
//---------------------------------------------------------------------------------------------------------------------
QPManager::QPManager(uint32_t maxQps)
: m_qpInactiveExpirySeconds(DEFAULT_QP_EXPIRY_SECONDS),
  m_cleanupIntervalSeconds(DEFAULT_QP_CLEANUP_INTERVAL_SECONDS),
  m_nActiveQps(0),
  m_criticalQpsLimit((maxQps * DEFAULT_QP_CRITICAL_LIMIT_PERCENT) / 100),
  m_vQpsMap(NUM_MAPS),
  m_vQpsLruQueue(NUM_MAPS),
  m_vQpsMutex(NUM_MAPS),
  m_stopCleanupThread(false)
{
    m_qpCleanupThread = make_unique<thread>([this]() {
        this->QPCleanupWorker();
    });

    LOG_INFO("Initialized QPManager qps limit={}, cleanupInterval={}, qp lru expiry={}",
        m_criticalQpsLimit, m_cleanupIntervalSeconds, m_qpInactiveExpirySeconds);
}
//---------------------------------------------------------------------------------------------------------------------
QPManager::~QPManager()
{
    m_stopCleanupThread = true;
    m_cvCleanupThread.notify_all();
    m_qpCleanupThread->join();
}
//---------------------------------------------------------------------------------------------------------------------
ibutils::shared_ibv_qp QPManager::CreateQP(ibv_cq* sendCQ, ibv_srq* receiveSRQ, ibv_pd* pd)
{
    struct ibv_qp_init_attr qpInitAttr{};
    qpInitAttr.send_cq = sendCQ;

    // The recv_cq is not used as we are setting srq(shared receive queue), but setting a dummy value here as this
    // is a required parameter.
    qpInitAttr.recv_cq = sendCQ;
    qpInitAttr.srq = receiveSRQ;

    // Setup reliable communication
    qpInitAttr.qp_type = IBV_QPT_RC;

    // There is only one message outstanding at any given time on the connection.
    // Allow 2 send WRs to absorb the rare overlap when a new request arrives via SRQ
    // before the previous send's ACK is received (SQ-full → ibv_post_send ENOMEM).
    qpInitAttr.cap.max_send_wr = 2;
    qpInitAttr.cap.max_recv_wr = 1;
    qpInitAttr.cap.max_send_sge = 1;
    qpInitAttr.cap.max_recv_sge = 1;

    // We don't use inline data as it is pretty small (around 60 bytes) and not sufficient for our messages
    qpInitAttr.cap.max_inline_data = 0;

    // We need to track all notifications (send and receive acknowledgement)
    qpInitAttr.sq_sig_all = 1;

    // Using custom deleter to track number of QPs
    shared_ibv_qp qp(ibv_create_qp(pd, &qpInitAttr), [this](ibv_qp* ibvQp)
        {
            if (ibvQp)
            {
                LOG_DEBUG("Closing qp={}", ibvQp->qp_num);
                int rc = ibv_destroy_qp(ibvQp);
                if (rc != 0)
                {
                    LOG_WARN("ibv_destroy_qp failed error={},{}", rc, strerror(errno));
                }
                else
                {
                    this->m_nActiveQps--;
                }
            }
        });
    if (!qp)
    {
        LOG_WARN("ibv_create_qp failed error={}", strerror(errno));
        return nullptr;
    }

    LOG_INFO("Created qp={}", qp->qp_num);

    // Cleanup QPs aggressively if we hit critical QP limit.
    // There could be multiple threads doing the same operation in corner case scenarios causing more than expected QP
    // deletions. Doing modulo 10 to reduce the worst case occurrence by 1 order of magnitude. Also doing this a 
    // few times should be fine since we have already hit high QP count. 
    uint32_t numQps = ++m_nActiveQps;
    if (numQps >= m_criticalQpsLimit && numQps % 10 == 0)
    {
        LOG_WARN("Num active qp={} crossed critical limit={}", numQps, m_criticalQpsLimit);

        // Iterate all the caches and remove one LRU entry from each bucket even if it has not expired
        for (uint32_t mapBucket = 0; mapBucket < NUM_MAPS; mapBucket++)
        {
            time_t currentTime = time(NULL);
            lock_guard lock(m_vQpsMutex[mapBucket]);
            if (!m_vQpsLruQueue[mapBucket].empty())
            {
                auto mapIter = m_vQpsMap[mapBucket].find(m_vQpsLruQueue[mapBucket].back().qpNum);
                if (mapIter == m_vQpsMap[mapBucket].end())
                {
                    LOG_WARN("qp already removed qp={}", m_vQpsLruQueue[mapBucket].back().qpNum);
                }
                else
                {
                    m_vQpsMap[mapBucket].erase(mapIter);
                }

                m_vQpsLruQueue[mapBucket].pop_back();
            }
        }

        LOG_INFO("Num active qp={} after cleanup", m_nActiveQps);
    }

    return qp;
}
//---------------------------------------------------------------------------------------------------------------------
bool QPManager::InsertQP(shared_ibv_qp qp)
{
    auto mapBucket = GetBucket(qp->qp_num);
    lock_guard lock(m_vQpsMutex[mapBucket]);

    auto iter = m_vQpsMap[mapBucket].find(qp->qp_num);
    if (iter != m_vQpsMap[mapBucket].end())
    {
        LOG_CRITICAL("qp_num already exists qp={}", qp->qp_num);
        return false;
    }

    m_vQpsLruQueue[mapBucket].push_front(QPLRUInfo(qp->qp_num, time(NULL)));
    auto listIter = m_vQpsLruQueue[mapBucket].begin();
    auto status = m_vQpsMap[mapBucket].insert(make_pair(qp->qp_num, QPInfo(qp, listIter)));
    if (status.second == false)
    {
        LOG_CRITICAL("qp_num already exists qp={}", qp->qp_num);
        return false;
    }
    else
    {
        LOG_DEBUG("Stored qp with num qp={}", qp->qp_num);
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
shared_ibv_qp QPManager::FindQP(uint32_t qpNum)
{
    auto mapBucket = GetBucket(qpNum);
    lock_guard lock(m_vQpsMutex[mapBucket]);
    auto mapIter = m_vQpsMap[mapBucket].find(qpNum);
    if (mapIter != m_vQpsMap[mapBucket].end())
    {

        // update lru list
        m_vQpsLruQueue[mapBucket].erase(mapIter->second.lruIterator);
        m_vQpsLruQueue[mapBucket].push_front(QPLRUInfo(qpNum, time(NULL)));
        auto listIter = m_vQpsLruQueue[mapBucket].begin();
        mapIter->second.lruIterator = listIter;
        return mapIter->second.ibvQp;
    }

    return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------
bool QPManager::DeleteQP(uint32_t qpNum)
{
    auto mapBucket = GetBucket(qpNum);
    lock_guard lock(m_vQpsMutex[mapBucket]);
    auto mapIter = m_vQpsMap[mapBucket].find(qpNum);
    if (mapIter == m_vQpsMap[mapBucket].end())
    {
        LOG_WARN("qp already removed qp={}", qpNum);
        return false;
    }

    m_vQpsLruQueue[mapBucket].erase(mapIter->second.lruIterator);
    m_vQpsMap[mapBucket].erase(mapIter);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
unsigned int QPManager::GetBucket(uint32_t qpNum)
{
    return qpNum % NUM_MAPS;
}
//---------------------------------------------------------------------------------------------------------------------
void QPManager::QPCleanupWorker()
{
    LOG_INFO("Started QPCleanupWorker. Sleep timer in seconds: {}", m_cleanupIntervalSeconds);
    while (!m_stopCleanupThread)
    {
        // wait until timeout or shutdown notification
        std::unique_lock<std::mutex> lock(m_mutexCleanupThread);
        if (m_cvCleanupThread.wait_for(
            lock,
            m_cleanupIntervalSeconds * 1s,
            [this]
            {
                return (this->m_stopCleanupThread == true);
            }) == true)
        {
            break;
        }

        LOG_INFO("Active QPs={} before expiry check", m_nActiveQps);

        // Iterate all the LRU caches and remove ALL expired entries per bucket
        time_t currentTime = time(NULL);
        for (uint32_t mapBucket = 0; mapBucket < NUM_MAPS; mapBucket++)
        {
            lock_guard lock(m_vQpsMutex[mapBucket]);
            while (!m_vQpsLruQueue[mapBucket].empty())
            {
                time_t lruTime = m_vQpsLruQueue[mapBucket].back().lruTime;
                if (difftime(currentTime, lruTime) < m_qpInactiveExpirySeconds)
                {
                    break;
                }

                // Expired entry, remove it from the data structures
                auto mapIter = m_vQpsMap[mapBucket].find(m_vQpsLruQueue[mapBucket].back().qpNum);
                if (mapIter == m_vQpsMap[mapBucket].end())
                {
                    LOG_WARN("qp already removed qp={}", m_vQpsLruQueue[mapBucket].back().qpNum);
                }
                else
                {
                    m_vQpsMap[mapBucket].erase(mapIter);
                }

                m_vQpsLruQueue[mapBucket].pop_back();
            }
        }

        LOG_INFO("Active QPs={} after expiry check", m_nActiveQps);
    }

    LOG_INFO("Shutdown QPCleanupWorker");
}
//---------------------------------------------------------------------------------------------------------------------