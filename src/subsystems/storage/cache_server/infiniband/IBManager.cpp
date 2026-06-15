///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IBManager.h"
#include "QPManager.h"
#include "IBConnection.h"
#include "HandleRequest.h"
#include "Constants.h"
#include "Logger.h"
#include "cache.pb.h"
#include "Metrics.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace ibutils;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
IBManager& IBManager::GetInstance()
{
    static IBManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
IBManager::IBManager()
: m_ibEnabled(false)
{
}
//---------------------------------------------------------------------------------------------------------------------
IBManager::~IBManager()
{
}
//---------------------------------------------------------------------------------------------------------------------
bool IBManager::IBEnabled() const
{
    return m_ibEnabled;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBManager::Init()
{
    if (m_ibEnabled)
    {
        LOG_ERROR("IBManager already initialized");
        return true;
    }

    unique_ibv_context context;
    bool bDeviceFound = GetIBDeviceInfo(m_ibDevice.lid, m_ibDevice.port, m_ibDevice.mtu, context);
    if (!bDeviceFound)
    {
        LOG_INFO("No active IB ports found");
        return false;
    }

    unique_ibv_pd pd(ibv_alloc_pd(context.get()), FreeIbvPd);
    if (!pd)
    {
        LOG_WARN("ibv_alloc_pd failed error={}", strerror(errno));
        return false;
    }

    unique_ibv_comp_channel compChannel(ibv_create_comp_channel(context.get()), FreeIbvCompChannel);
    if (!compChannel)
    {
        LOG_WARN("ibv_create_comp_channel failed error={}", strerror(errno));
        return false;
    }

    unique_ibv_cq sendCq(ibv_create_cq(context.get(), MAX_REQUESTS, NULL, compChannel.get(), 0), FreeIbvCq);
    if (!sendCq)
    {
        LOG_WARN("ibv_create_cq failed error={}", strerror(errno));
        return false;
    }

    // Register completion queue to receive notifications
    int rc = ibv_req_notify_cq(sendCq.get(), 0);
    if (rc != 0)
    {
        LOG_WARN("ibv_req_notify_cq failed error={},{}", rc, strerror(errno));
        return false;
    }

    struct ibv_srq_init_attr srq_init_attr{};
    srq_init_attr.attr.max_wr  = MAX_RECEIVE_BUFFERS;
    srq_init_attr.attr.max_sge = 1;
    unique_ibv_srq receiveSRQ(ibv_create_srq(pd.get(), &srq_init_attr), FreeIbvSrq);
    if (!receiveSRQ)
    {
        LOG_WARN("ibv_create_srq failed error={}", strerror(errno));
        return false;
    }

    // Register a large MR block and split it into multiple equal sized buffers
    uint64_t nMRBufferSize = static_cast<uint64_t>(MAX_RECEIVE_BUFFERS) * IB_BUFFER_SIZE;
    std::unique_ptr<char[]> mrBufferBlock(new char[nMRBufferSize]);
    unique_ibv_mr mrBuffer(ibv_reg_mr(pd.get(), mrBufferBlock.get(),
        nMRBufferSize, static_cast<int>(IBV_ACCESS_LOCAL_WRITE)), FreeIbvMr);

    if (!mrBuffer)
    {
        LOG_WARN("ibv_reg_mr failed error={}", strerror(errno));
        return false;
    }

    struct ibv_device_attr devAttr{};
    rc = ibv_query_device(context.get(), &devAttr);
    if (rc != 0)
    {
        LOG_WARN("ibv_query_device failed error={},{}", rc, strerror(errno));
        return false;
    }

    m_ibDevice.context = move(context);
    m_ibDevice.pd = move(pd);
    m_ibDevice.compEventChannel = move(compChannel);
    m_ibDevice.sendCQ = move(sendCq);
    m_ibDevice.receiveSRQ = move(receiveSRQ);
    m_ibDevice.mrBuffer = move(mrBuffer);
    m_ibDevice.mrBufferBlock = move(mrBufferBlock);

    LOG_INFO("Configured IB device: {},{},{},{},{},{}",
        m_ibDevice.port,
        m_ibDevice.lid,
        m_ibDevice.mtu,
        m_ibDevice.sendCQ->cqe,
        devAttr.max_qp,
        devAttr.max_qp_wr);

    // Post receive requests to SRQ
    for (int i = 0; i < MAX_RECEIVE_BUFFERS; i++)
    {
        uint64_t offset = static_cast<uint64_t>(i) * IB_BUFFER_SIZE;
        char* bufferAddr = m_ibDevice.mrBufferBlock.get() + offset;
        PostWrToSRQ((uint64_t)bufferAddr);
    }

    // Start IB listener and handler threads
    auto thread = new std::thread(&IBManager::IBListenerThread, this);
    for (int i = 0; i < IB_RECEIVE_THREADS; ++i)
    {
        auto thread = new std::thread(&IBManager::RequestHandlerThread, this);
    }

    m_qpManager = make_unique<QPManager>(devAttr.max_qp);
    m_ibEnabled = true;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBManager::SetupQPConnections(
    uint32_t clientLid,
    uint8_t clientIbPort,
    uint8_t clientMtu,
    const ::google::protobuf::RepeatedField<::google::protobuf::uint32>& clientQpns,
    uint32_t& serverLid,
    uint8_t& serverIbPort,
    uint8_t& serverMtu,
    ::google::protobuf::RepeatedField<::google::protobuf::uint32>& serverQpns)
{
    if (!m_ibEnabled)
    {
        LOG_ERROR("IB is not setup, cannot create QP connections");
        return false;
    }

    serverLid = m_ibDevice.lid;
    serverIbPort = m_ibDevice.port;
    serverMtu = (uint8_t)m_ibDevice.mtu;

    // Establish one to one qp mapping for client-server communication
    serverQpns.Clear();

    for (int qpn = 0; qpn < clientQpns.size(); qpn++)
    {
        shared_ibv_qp qp = m_qpManager->CreateQP(
            m_ibDevice.sendCQ.get(), m_ibDevice.receiveSRQ.get(), m_ibDevice.pd.get());
        if (!qp)
        {
            LOG_WARN("Failed to create QP, rolling back {} QPs for client={}", serverQpns.size(), clientLid);
            for (int i = 0; i < serverQpns.size(); i++)
            {
                m_qpManager->DeleteQP(serverQpns.Get(i));
            }
            serverQpns.Clear();
            return false;
        }

        bool bRet = SetupQPForSendAndReceive(
            qp.get(),
            m_ibDevice.port,

            // Using minimum of server and client mtu to ensure both are configured with the same mtu value
            (ibv_mtu)std::min((uint8_t)m_ibDevice.mtu, clientMtu),
            clientQpns.Get(qpn),
            clientLid,
            clientIbPort);
        if (!bRet)
        {
            LOG_WARN("Failed to setup QP id={}, rolling back {} QPs for client={}", qp->qp_num, serverQpns.size(), clientLid);
            for (int i = 0; i < serverQpns.size(); i++)
            {
                m_qpManager->DeleteQP(serverQpns.Get(i));
            }
            serverQpns.Clear();
            return false;
        }

        serverQpns.Add(qp->qp_num);
        m_qpManager->InsertQP(qp);
    }

    LOG_INFO("Setup QP connections successfully: {},{},{},{}/{},{},{},{}",
        clientLid, clientIbPort, clientMtu, clientQpns.size(), serverLid, serverIbPort, serverMtu, serverQpns.size())
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void IBManager::IBListenerThread()
{
    LOG_DEBUG("IBListenerThread started")
    while (true)
    {
        // Wait for the Completion event
        LOG_DEBUG("Wait for cq event");
        struct ibv_cq* ev_cq = nullptr;
        void* ev_ctx = nullptr;
        int rc = ibv_get_cq_event(m_ibDevice.compEventChannel.get(), &ev_cq, &ev_ctx);
        if (rc != 0)
        {
            LOG_CRITICAL("ibv_get_cq_event failed error={},{}", rc, strerror(errno));
            exit(-1);
        }

        LOG_DEBUG("Received cq event");

        // Ack the event
        ibv_ack_cq_events(ev_cq, 1);
        
        // Request notification upon the next completion event
        rc = ibv_req_notify_cq(ev_cq, 0);
        if (rc != 0)
        {
            LOG_CRITICAL("ibv_req_notify_cq error={},{}", rc, strerror(errno));
            exit(-1);
        }
        
        // Empty the CQ: poll all of the completions from the CQ (if any exist)
        int nEvents = 0;
        do
        {
            LOG_DEBUG("poll cq events");

            // We read one event at a time as only one event is expected to be outstanding according to our 
            // request/response protocol. One thread handles only one request from a client.
            // New requests will be handled in the next loop iteration after the current request is processed.
            // If there are multiple events in the queue it will be polled without any event loss in the do/while loop.
            struct ibv_wc wc{};
            nEvents = ibv_poll_cq(ev_cq, 1, &wc);
            if (nEvents < 0) {
                LOG_CRITICAL("ibv_poll_cq failed error={},{}", nEvents, strerror(errno));
                exit(-1);
            }

            // there may be an extra event with no completion in the CQ
            LOG_DEBUG("Polled completion queue, num events={}", nEvents);
            if (nEvents == 0)
            {
                continue;
            }

            if (wc.status != IBV_WC_SUCCESS)
            {
                LOG_WARN("Received work completion error={},{} addr={} qp={}",
                    ibv_wc_status_str(wc.status), wc.vendor_err, wc.wr_id, wc.qp_num);

                // Reclaim the memory buffer
                PostWrToSRQ(wc.wr_id);

                // Peer death or QP error — move QP to error state and remove from manager
                // to prevent QP table exhaustion (mlx5 device cap is 2^17 QPs).
                shared_ibv_qp errQp = m_qpManager->FindQP(wc.qp_num);
                if (errQp)
                {
                    struct ibv_qp_attr qpAttr{};
                    qpAttr.qp_state = IBV_QPS_ERR;
                    int rc = ibv_modify_qp(errQp.get(), &qpAttr, IBV_QP_STATE);
                    if (rc != 0)
                    {
                        LOG_WARN("ibv_modify_qp to ERR failed qp={} error={},{}", wc.qp_num, rc, strerror(errno));
                    }
                    m_qpManager->DeleteQP(wc.qp_num);
                    LOG_INFO("Deleted QP on error qp={}", wc.qp_num);
                }
                continue;
            }

            if (wc.opcode == IBV_WC_SEND)
            {
                // We can reclaim the buffer
                LOG_DEBUG("Received send completion: qp={} addr={}", wc.qp_num, wc.wr_id);
                PostWrToSRQ(wc.wr_id);
                continue;
            }

            if (wc.opcode != IBV_WC_RECV)
            {
                LOG_CRITICAL("Received unexpected completion opcode: {}", wc.opcode);
                continue;
            }

            LOG_DEBUG("Received receive work request addr={} qp={}", wc.wr_id, wc.qp_num);

            // Allocate handler to process this work request
            {
                lock_guard lock(m_mutexIBRequestQ);
                m_ibRequestQueue.push(wc);
            }
            m_cvQueue.notify_one();
        } while (nEvents > 0);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void IBManager::RequestHandlerThread()
{
    LOG_INFO("Started RequestHandlerThread");
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_mutexIBRequestQ);
        m_cvQueue.wait(lock,
            [this]
            {
                return this->m_ibRequestQueue.size() > 0;
            });

        ibv_wc wc{};
        if (m_ibRequestQueue.size() > 0)
        {
            wc = m_ibRequestQueue.front();
            m_ibRequestQueue.pop();
        }
        else
        {
            LOG_WARN("No requests to process in queue");
            continue;
        }

        lock.unlock();
        ProcessRequest(wc);
    }
    LOG_INFO("Shutdown RequestHandlerThread");
}
//---------------------------------------------------------------------------------------------------------------------
void IBManager::ProcessRequest(const ibv_wc& wc)
{
    LOG_DEBUG("Handle receive request qp={}, addr={}, bytes={}", wc.qp_num, wc.wr_id, wc.byte_len);
    shared_ibv_qp qp = m_qpManager->FindQP(wc.qp_num);
    if (!qp)
    {
        // QP was already deleted (e.g. peer death detected via error WC while this request was queued).
        // Reclaim the buffer to avoid exhausting the SRQ buffer pool.
        LOG_WARN("QP already removed, discarding request qp={}", wc.qp_num);
        PostWrToSRQ(wc.wr_id);
        return;
    }

    Metrics::IncrementActiveConnections();
    auto start_time = std::chrono::steady_clock::now();

    IBConnection ibConnection(qp, (char*) wc.wr_id, m_ibDevice.mrBuffer->lkey, wc.byte_len);
    Request::TypeCase eRequestType = Request::TypeCase::TYPE_NOT_SET;
    ResponseCode eResponseCode = ResponseCode::SUCCESS;
    std::size_t nFileSize = 0;
    bool bRet = HandleRequest(ibConnection, eRequestType, eResponseCode, nFileSize);
    if (bRet)
    {
        LOG_DEBUG("Processed request successfully. qp={}, addr={}", wc.qp_num, wc.wr_id);
    }
    else
    {
        LOG_WARN("Failed to process request. qp={} addr={}", wc.qp_num, wc.wr_id);

        // Reclaim the memory buffer
        PostWrToSRQ(wc.wr_id);

        // Set this QP to error state to drain all pending work requests on this QP.
        // Otherwise there could be orphaned WRs in the send/receive queue potentially causing queue overruns.
        struct ibv_qp_attr qpAttr{};
        qpAttr.qp_state = IBV_QPS_ERR;
        int rc = ibv_modify_qp(qp.get(), &qpAttr, IBV_QP_STATE);
        if (rc != 0)
        {
            // Can't do much if this fails, just log and continue
            LOG_WARN("ibv_modify_qp failed error={},{}", rc, strerror(errno));
        }

        // Cleanup QP from server
        m_qpManager->DeleteQP(qp->qp_num);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    Metrics::RegisterRequest(eRequestType, eResponseCode, duration.count(), nFileSize);
    Metrics::DecrementActiveConnections();
}
//---------------------------------------------------------------------------------------------------------------------
void IBManager::PostWrToSRQ(uint64_t bufferAddr)
{
    struct ibv_sge sg{};
    sg.addr	  = (uintptr_t)(bufferAddr);
    sg.length = IB_BUFFER_SIZE;
    sg.lkey	  = m_ibDevice.mrBuffer->lkey;

    struct ibv_recv_wr wr{};
    wr.wr_id      = bufferAddr;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    struct ibv_recv_wr* bad_wr = nullptr;
    int rc = ibv_post_srq_recv(m_ibDevice.receiveSRQ.get(), &wr, &bad_wr);
    if (rc != 0)
    {
        // TODO: This memory buffer is now lost, just logging for now to check if this ever happens
        LOG_CRITICAL("ibv_post_srq_recv failed error={},{}", rc, strerror(errno));
    }
    else
    {
        LOG_DEBUG("Posted receive wr addr={}", bufferAddr);
    }
}
//---------------------------------------------------------------------------------------------------------------------