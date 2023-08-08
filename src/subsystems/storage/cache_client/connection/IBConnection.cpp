///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IBConnection.h"
#include "CommonLogger.h"
#include "StorageLibcApiWrapper.h"
#include <fcntl.h>
#include <poll.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace ibutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
IBConnection::IBConnection(unique_ibv_qp qp, unique_ibv_comp_channel eventChannel, unique_ibv_cq cq, unique_ibv_mr mr,
    unique_ptr<char[]> mrBuffer)
    : m_qp(move(qp)), m_eventChannel(move(eventChannel)), m_cq(move(cq)), m_mr(move(mr)), m_mrBuffer(move(mrBuffer))
{

    // Fetch and change the blocking mode of the completion channel to non-blocking
    int flags = fcntl(m_eventChannel->fd, F_GETFL);
    if (flags < 0) {
        LOG_CRITICAL("Couldn't get flags for channel fd={}, error={},{}", m_eventChannel->fd, flags, strerror(errno));
        throw "Couldn't get flags for channel fd";
    }

    int rc = fcntl(m_eventChannel->fd, F_SETFL, flags | O_NONBLOCK);
    if (rc < 0) {
        LOG_CRITICAL("Couldn't set channel fd={} to non-blocking, error={},{}", m_eventChannel->fd, rc, strerror(errno));
        throw "Couldn't set channel fd to non-blocking";
    }

    ResetConnection();
}
//---------------------------------------------------------------------------------------------------------------------
IBConnection::~IBConnection()
{
    LOG_INFO("Closing qp={}", GetId());
    m_qp.reset(nullptr);
    m_mr.reset(nullptr);
    m_cq.reset(nullptr);
    m_eventChannel.reset(nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
void IBConnection::ResetConnection()
{
    m_writeOffset = 0;
    m_responseLength = -1;
    m_readOffset = 0;
    m_qpError = false;
    m_sendAttempted = false;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBConnection::FinalizeWrite()
{
    if (m_sendAttempted)
    {
        LOG_CRITICAL("Remote send already attempted qp={}", GetId());
        return false;
    }
    
    m_sendAttempted = true;

    // Prepare to receive response before sending request
    struct ibv_sge sg{};
    sg.addr	  = (uintptr_t)(m_mrBuffer.get());
    sg.length = IB_BUFFER_SIZE;
    sg.lkey	  = m_mr->lkey;
    
    struct ibv_recv_wr recv_wr{};
    recv_wr.wr_id      = (uint64_t)sg.addr;
    recv_wr.sg_list    = &sg;
    recv_wr.num_sge    = 1;
    
    struct ibv_recv_wr* recv_bad_wr = nullptr;
    int rc = ibv_post_recv(m_qp.get(), &recv_wr, &recv_bad_wr);
    if (rc != 0) {
        LOG_ERROR("ibv_post_recv() failed qp={} error={},{}", GetId(), rc, strerror(errno));
        m_qpError = true;
        return false;
    }

    // We can reuse the sg structure
    memset(&sg, 0, sizeof(sg));
    sg.addr	  = (uintptr_t)m_mrBuffer.get();
    sg.length = m_writeOffset;
    sg.lkey	  = m_mr->lkey;
    
    struct ibv_send_wr wr{};
    wr.wr_id      = (uint64_t)m_mrBuffer.get();
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    struct ibv_send_wr* bad_wr = nullptr;
    rc = ibv_post_send(m_qp.get(), &wr, &bad_wr);
    if (rc != 0)
    {
        LOG_WARN("ibv_post_send() failed qp={} error={},{}", GetId(), rc, strerror(errno));
        m_qpError = true;
        return false;
    }

    LOG_DEBUG("Sent request on qp={}", GetId());
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::Send(const char* buffer, uint64_t size)
{
    LOG_DEBUG("Send qp={} size={}", GetId(), size);
    if (m_writeOffset + size > IB_BUFFER_SIZE)
    {
        LOG_ERROR("Write size exceeds buffer limit qp={} total size={} limit={}",
            GetId(), m_writeOffset + size, IB_BUFFER_SIZE);
        m_qpError = true;
        return -1;
    }

    memcpy(m_mrBuffer.get() + m_writeOffset, buffer, size);
    m_writeOffset += size;
    return size;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::SendAndFinalize(const char* buffer, uint64_t size)
{
    LOG_DEBUG("SendAndFinalize qp={} size={}", GetId(), size);
    int bytesSent = Send(buffer, size);
    if (bytesSent >= 0 && !FinalizeWrite())
    {
        LOG_WARN("Failed to send request to remote qp={}", GetId());
        m_qpError = true;
        return -1;
    }

    return bytesSent;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::Receive(char* buffer, uint64_t size)
{
    LOG_DEBUG("Read from IB qp={} size={}", GetId(), size);
    if (m_responseLength < 0)
    {
        LOG_DEBUG("Read from IB remote qp={} size={}", GetId(), size);

        // Read buffer from remote
        bool bResponse = false;
        while (!bResponse)
        {

            // Wait for some max timeout to prevent hangs
            struct pollfd pollFd{};
            pollFd.fd = m_eventChannel->fd;
            pollFd.events = POLLIN;
            int rc = poll(&pollFd, 1, IB_READ_TIMEOUT_MILLIS);
            if (rc < 0) {
                LOG_WARN("Failed to poll on event fd={} error={},{}", pollFd.fd, rc, strerror(errno));
                m_qpError = true;
                return -1;
            }

            if (rc == 0)
            {
                LOG_WARN("poll timed out waiting for read event fd={}", pollFd.fd);
                m_qpError = true;
                return -1;
            }

            struct ibv_cq* ev_cq = nullptr;
            void *ev_ctx = nullptr;

            // Wait for the Completion event
            LOG_DEBUG("Check cq event qp={}", GetId());
            rc = ibv_get_cq_event(m_eventChannel.get(), &ev_cq, &ev_ctx);
            if (rc != 0)
            {
                LOG_WARN("Failed to get CQ event qp={} error={},{}", GetId(), rc, strerror(errno));
                m_qpError = true;
                return -1;
            }

            LOG_DEBUG("Received cq event id={}", GetId());

            // Ack the event
            ibv_ack_cq_events(ev_cq, 1);
            
            // Request notification upon the next completion event
            rc = ibv_req_notify_cq(ev_cq, 0);
            if (rc != 0)
            {
                LOG_ERROR("Failed to request CQ notification qp={} error={},{}", GetId(), rc, strerror(errno));
                m_qpError = true;
                return -1;
            }
            
            // Empty the CQ: poll all of the completions from the CQ (if any exist)
            int nEvents = 0;
            do
            {
                struct ibv_wc wc{};
                nEvents = ibv_poll_cq(ev_cq, 1, &wc);
                if (nEvents < 0)
                {
                    LOG_ERROR("Failed to poll completions from the CQ. qp={} error={},{}", GetId(), nEvents,
                        strerror(errno));
                    m_qpError = true;
                    return -1;
                }

                // there may be an extra event with no completion in the CQ
                if (nEvents == 0)
                {
                    continue;
                }

                if (wc.status != IBV_WC_SUCCESS)
                {
                    LOG_ERROR("Completion failed qp={} error={}", GetId(), ibv_wc_status_str(wc.status));
                    m_qpError = true;
                    return -1;
                }

                LOG_DEBUG("Received successful cq event qp={} addr={} qp_num={}", GetId(), wc.wr_id, wc.qp_num);
                if (wc.opcode == IBV_WC_SEND)
                {
                    LOG_DEBUG("Sent request successfully qp={}", GetId());
                }
                else if (wc.opcode == IBV_WC_RECV)
                {
                    bResponse = true;
                    m_responseLength = wc.byte_len;
                    LOG_DEBUG("Received response successfully qp={} bytes={}", GetId(), m_responseLength);
                    break;
                }
            }
            while (nEvents);
        }
    }

    if (m_responseLength > 0)
    {
        int readBytes = std::min((int)size, m_responseLength - (int)m_readOffset);
        if (readBytes > 0)
        {
            memcpy(buffer, m_mrBuffer.get() + m_readOffset, readBytes);
            m_readOffset += readBytes;
            LOG_DEBUG("Read bytes={} from IB buffer qp={}", readBytes, GetId());
        }
        else
        {
            LOG_WARN("End of read buffer qp={} readOffset={} readLength={} size={}",
                GetId(), m_readOffset, m_responseLength, size);
        }
        return readBytes;
    }

    return m_responseLength;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::GetId()
{
    return m_qp->qp_num;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBConnection::IsValidConnection()
{
    return !m_qpError;
}
//---------------------------------------------------------------------------------------------------------------------