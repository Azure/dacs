///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "SocketConnection.h"
#include "StorageInterceptLogger.h"
#include "StorageLibcApiWrapper.h"
#include "poll.h"
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::socketTimeoutMillis = 5000;
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::SocketConnection(int socketFd)
: m_socketFd(socketFd)
{
    ResetConnection();
}
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::~SocketConnection()
{
    LOG_INFO("Close server fd={}", m_socketFd);
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
        if ((tempWrite = TimedWrite(
            buffer + totalWritten,
            bufferLen)) > 0)
        {
            totalWritten += tempWrite;
        }
        else 
        {
            LOG_WARN("couldn't complete write to server: fd={}. wrote={}, error={}, errno={}:{}",
                GetId(), totalWritten, tempWrite, errno, strerror(errno));
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
void SocketConnection::__UnitTest_SetSocketTimeoutMillis(int timeoutMillis)
{
    SocketConnection::socketTimeoutMillis = timeoutMillis;
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

    int pollResult = poll(pollFDs, 1, socketTimeoutMillis);
    if (pollResult == 0)
    {
        LOG_WARN("Timeout reading from server, fd={}", m_socketFd);
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

    int pollResult = poll(pollFDs, 1, socketTimeoutMillis);
    if (pollResult == 0)
    {
        LOG_WARN("Timeout writing to server, fd={}", m_socketFd);
        return -1;
    }
    if (pollResult < 0 || pollFDs[0].revents != POLLOUT)
    {
        LOG_WARN("Error listening to write event, fd={}, pollResult={}, pollpollFDs[0].revents={}, Result errno: {}:{}",
                 m_socketFd,
                 pollResult,
                 pollFDs[0].revents,
                 errno,
                 strerror(errno));
        return -1;
    }

    return send(m_socketFd, buf, nByte, 0);
}
//---------------------------------------------------------------------------------------------------------------------