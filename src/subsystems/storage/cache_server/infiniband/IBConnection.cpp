///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IBConnection.h"
#include "IBConstants.h"
#include "Logger.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace ibutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
IBConnection::IBConnection(shared_ibv_qp qp, char* buffer, uint32_t mrKey, int bytesReceived)
: m_qp(qp), m_buffer(buffer), m_mrKey(mrKey), m_nDataReadFromRemote(bytesReceived),
  m_readOffset(0), m_writeOffset(0), m_sendAttempted(false)
{
}
//---------------------------------------------------------------------------------------------------------------------
IBConnection::~IBConnection()
{
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::Read(char* buffer, int size)
{
    int readBytes = std::min(size, m_nDataReadFromRemote - m_readOffset);
    if (readBytes > 0)
    {
        memcpy(buffer, m_buffer + m_readOffset, readBytes);
        m_readOffset += readBytes;
    }
    else
    {
        LOG_WARN("End of read buffer, id={}, readOffset={}, m_nDataReadFromRemote={}, size={}",
            GetId(), m_readOffset, m_nDataReadFromRemote, size);
    }
    return readBytes;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBConnection::FinalizeWrite()
{
    if (m_sendAttempted)
    {
        LOG_CRITICAL("Attempting to resend the response qp={}", GetId());
        return false;
    }

    m_sendAttempted = true;

    struct ibv_sge sg{};
    sg.addr	  = (uintptr_t)m_buffer;
    sg.length = m_writeOffset;
    sg.lkey	  = m_mrKey;
    
    struct ibv_send_wr wr{};
    wr.wr_id      = (uint64_t)m_buffer;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    struct ibv_send_wr* bad_wr = nullptr;
    int rc = ibv_post_send(m_qp.get(), &wr, &bad_wr);
    if (rc != 0)
    {
        LOG_WARN("ibv_post_send() failed, qp={} addr={} error={},{}", GetId(), wr.wr_id, rc, strerror(errno));
        return false;
    }

    LOG_DEBUG("Sent response on qp={}", GetId());
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::Write(char* buffer, int size)
{
    if (m_writeOffset + size > IB_BUFFER_SIZE)
    {
        LOG_WARN("Not enough buffer space to complete write. qp={} addr={} buffersize={} writeLen={}",
            GetId(), (uint64_t)m_buffer, IB_BUFFER_SIZE, m_writeOffset + size);
        return -1;
    }

    if (size > 0)
    {
        memcpy(m_buffer + m_writeOffset, buffer, size);
        m_writeOffset += size;
    }
    else if (size < 0)
    {
        LOG_WARN("Invalid size qp={} size={}", GetId(), size);
        return -1;
    }

    return size;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::WriteFinal(char* buffer, int size)
{
    int bytesWritten = Write(buffer, size);
    if (bytesWritten >= 0)
    {
        if (!FinalizeWrite())
        {
            LOG_WARN("Failed to send response to remote. qp={}", GetId());
            return -1;
        }
    }

    return bytesWritten;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBConnection::Sendfile(int fileFd, uint64_t fileStartOffset, uint64_t totalLength)
{
    if (m_writeOffset + totalLength > IB_BUFFER_SIZE)
    {
        LOG_WARN("Not enough buffer space to complete write. qp={} addr={} buffersize={} writeLen={}",
            GetId(), (uint64_t)m_buffer, IB_BUFFER_SIZE, m_writeOffset + totalLength);
        return -1;
    }

    // loop to serve entire file
    if (fileStartOffset > 0)
    {
        off_t fileOffset = lseek(fileFd, fileStartOffset, SEEK_SET);
        if (fileOffset != fileStartOffset)
        {
            LOG_WARN("Failed to set file offset. fileFd={} error={},{} offset={}/{}",
                fileFd, errno, strerror(errno), fileStartOffset, fileOffset);
            return false;
        }
    }

    uint64_t totalRead = 0;
    while (totalRead < totalLength)
    {        
        ssize_t readBytes = read(fileFd, m_buffer + m_writeOffset + totalRead, totalLength - totalRead);
        if (readBytes <= 0)
        {
            LOG_WARN("Error during read fileFd={} error={}:{}. total bytes read={}",
                fileFd, errno, strerror(errno), totalRead);
            return false;
        }
        totalRead += readBytes;
    }
    m_writeOffset += totalRead;
    return FinalizeWrite();
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::GetId()
{
    return m_qp->qp_num;
}
//---------------------------------------------------------------------------------------------------------------------
