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
    if (m_readOffset >= m_nDataReadFromRemote)
    {
        LOG_WARN("End of read buffer, id={}, readOffset={}, m_nDataReadFromRemote={}, size={}",
            GetId(), m_readOffset, m_nDataReadFromRemote, size);
        return 0;
    }

    int readBytes = std::min(size, m_nDataReadFromRemote - m_readOffset);
    memcpy(buffer, m_buffer + m_readOffset, readBytes);
    m_readOffset += readBytes;
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
        LOG_WARN("ibv_post_send() failed, qp={} addr={} error={},{}", GetId(), wr.wr_id, rc, strerror(rc));
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
        return false;
    }

    // seek to the specified offset in the file before reading
    off_t lseekResult = lseek(fileFd, fileStartOffset, SEEK_SET);
    if (lseekResult != static_cast<off_t>(fileStartOffset))
    {
        LOG_WARN("Failed to set file offset. fileFd={} error={},{} offset={}/{}",
            fileFd, errno, strerror(errno), fileStartOffset, lseekResult);
        return false;
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
bool IBConnection::Recvfile(int fileFd, uint64_t totalLength)
{
    // IB connections transfer data in a single RDMA read into m_buffer.
    // splice() is not applicable here, so we write from m_buffer to file in chunks.
    static constexpr uint64_t MAX_WRITE_CHUNK = 4 * 1024 * 1024;
    uint64_t totalWritten = 0;

    if (totalLength > static_cast<uint64_t>(m_nDataReadFromRemote - m_readOffset))
    {
        LOG_WARN("Recvfile: requested {} bytes but only {} available in IB buffer, qp={}",
                 totalLength, m_nDataReadFromRemote - m_readOffset, GetId());
        return false;
    }

    while (totalWritten < totalLength)
    {
        uint64_t toWrite = std::min(MAX_WRITE_CHUNK, totalLength - totalWritten);
        if (m_readOffset >= m_nDataReadFromRemote)
        {
            LOG_WARN("Recvfile: end of IB read buffer, qp={}, readOffset={}, totalLength={}, written={}",
                     GetId(), m_readOffset, totalLength, totalWritten);
            return false;
        }

        char* src = m_buffer + m_readOffset;
        int bytesToWrite = std::min(static_cast<int>(toWrite), m_nDataReadFromRemote - m_readOffset);

        ssize_t w = ::write(fileFd, src, bytesToWrite);
        if (w <= 0)
        {
            if (w < 0 && errno == EINTR)
            {
                continue;
            }
            LOG_WARN("Recvfile: write to file failed, fileFd={}, errno={}:{}, written={}/{}",
                     fileFd, errno, strerror(errno), totalWritten, totalLength);
            return false;
        }

        m_readOffset += w;
        totalWritten += w;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
int IBConnection::GetId()
{
    return m_qp->qp_num;
}
//---------------------------------------------------------------------------------------------------------------------
