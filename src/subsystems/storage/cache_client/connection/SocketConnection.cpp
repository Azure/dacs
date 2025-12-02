///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "SocketConnection.h"
#include "CommonLogger.h"
#include "StorageLibcApiWrapper.h"
#include "poll.h"
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
uint32_t SocketConnection::socketTimeoutMillisMin = 20000;
uint32_t SocketConnection::socketTimeoutMillisPerMB = 2000;
//---------------------------------------------------------------------------------------------------------------------
/*static*/ int SocketConnection::GetTimeoutMillis(size_t ioSizeInBytes)
{
    constexpr uint32_t ONE_MB = 1048576;
    return max(socketTimeoutMillisMin,
               socketTimeoutMillisPerMB * (uint32_t)((ioSizeInBytes + ONE_MB) / ONE_MB));
}
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::SocketConnection(int socketFd)
: m_socketFd(socketFd)
{
    ResetConnection();
}
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::~SocketConnection()
{
    LOG_DEBUG("Close server fd={}", m_socketFd);
    StorageLibcApiWrapper::close(m_socketFd);
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::SendAndFinalize(const char* buffer, uint64_t size)
{
    if (m_sendAttempted)
    {
        LOG_CRITICAL("Remote send already attempted fd={}", GetId());
        return -1;
    }

    m_sendAttempted = true;
    return Send(buffer, size);
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::Send(const char* buffer, uint64_t size)
{
    uint64_t totalWritten = 0;
    while (totalWritten < size)
    {
        int bufferLen = size - totalWritten;
        int tempWrite;
        if ((tempWrite = TimedWrite(buffer + totalWritten, bufferLen)) > 0)
        {
            totalWritten += tempWrite;
        }
        else 
        {
            LOG_WARN("couldn't complete write to server: fd={}. wrote={} of size={}, error={}, errno={}:{}",
                GetId(), totalWritten, size, tempWrite, errno, strerror(errno));
            return totalWritten;
        }
    }

    return totalWritten;
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::Receive(char* buffer, uint64_t size)
{
    return TimedRead(buffer, size);
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::GetId()
{
    return m_socketFd;
}
//---------------------------------------------------------------------------------------------------------------------
bool SocketConnection::IsValidConnection()
{
    if (recv(m_socketFd, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
    {
        LOG_DEBUG("Found stale fd={}", m_socketFd);
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void SocketConnection::ResetConnection()
{
    m_sendAttempted = false;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t SocketConnection::TimedRead(void* buf, size_t nByte)
{
    struct pollfd pollFDs[1];
    memset(pollFDs, 0 , sizeof(pollFDs));
    pollFDs[0].fd = m_socketFd;
    pollFDs[0].events = POLLIN;

    const int timeoutMillis = GetTimeoutMillis(nByte);
    int pollResult = poll(pollFDs, 1, timeoutMillis);
    if (pollResult == 0)
    {
        LOG_WARN("Timeout reading from server, fd={}, timeoutMillis={}", m_socketFd, timeoutMillis);
        return -1;
    }
    if (pollResult < 0 || pollFDs[0].revents != POLLIN)
    {
        LOG_WARN("Error listening to read event, fd={}, pollResult={}, pollpollFDs[0].revents={}, Result errno: {}:{}",
                 m_socketFd,
                 pollResult,
                 pollFDs[0].revents,
                 errno,
                 strerror(errno));
        return -1;
    }

    return recv(m_socketFd, buf, nByte, 0);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t SocketConnection::TimedWrite(const void* buf, size_t nByte)
{
    struct pollfd pollFDs[1];
    memset(pollFDs, 0 , sizeof(pollFDs));
    pollFDs[0].fd = m_socketFd;
    pollFDs[0].events = POLLOUT;

    const int socketTimeoutMillis = GetTimeoutMillis(nByte);
    int pollResult = poll(pollFDs, 1, socketTimeoutMillis);
    if (pollResult == 0)
    {
        LOG_WARN("Timeout writing to server, fd={}, timeoutMillis={}", m_socketFd, socketTimeoutMillis);
        return -1;
    }
    if (pollResult < 0)
    {
        LOG_WARN("poll() error before write, fd={}, pollResult={}, errno={}:{}",
                 m_socketFd,
                 pollResult,
                 errno,
                 strerror(errno));
        return -1;
    }

    short re = pollFDs[0].revents;
    // Handle error / hangup conditions first
    if (re & POLLERR)
    {
        LOG_WARN("Socket error flagged before write, fd={}, revents={}, errno={}:{}",
                 m_socketFd,
                 re,
                 errno,
                 strerror(errno));
        return -1;
    }
    if (re & POLLHUP)
    {
        LOG_WARN("Socket hangup before write, fd={}, revents={}, errno={}:{}",
                 m_socketFd,
                 re,
                 errno,
                 strerror(errno));
        return -1;
    }
    if (re & POLLNVAL)
    {
        LOG_WARN("Invalid socket (POLLNVAL) before write, fd={}, revents={}, errno={}:{}",
                 m_socketFd,
                 re,
                 errno,
                 strerror(errno));
        return -1;
    }

    if (!(re & POLLOUT))
    {
        LOG_WARN("POLLOUT not set for write, fd={}, revents={}, errno={}:{}",
                 m_socketFd,
                 re,
                 errno,
                 strerror(errno));
        return -1;
    }

    // Ready to write
    uint16_t nAttempts = 0;
    ssize_t written;
    do
    {
        written = send(m_socketFd, buf, nByte, 0);
        nAttempts++;
    }
    while (written < 0 && errno == EINTR && nAttempts < 5);

    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        LOG_WARN("Transient send unavailability after POLLOUT, fd={}, errno={}:{}",
                 m_socketFd,
                 errno,
                 strerror(errno));
    }
    return written;
}
//---------------------------------------------------------------------------------------------------------------------