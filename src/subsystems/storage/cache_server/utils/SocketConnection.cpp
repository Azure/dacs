///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "SocketConnection.h"
#include "Logger.h"
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::SocketConnection(int socketFd)
: m_socketFd(socketFd), m_corkState(0)
{
}
//---------------------------------------------------------------------------------------------------------------------
SocketConnection::~SocketConnection()
{
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::Read(char* buffer, int size)
{
    return read(m_socketFd, buffer, size);
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::Write(char* buffer, int size)
{
    if (m_corkState == 0)
    {
        m_corkState = 1;
        if (setsockopt(m_socketFd, IPPROTO_TCP, TCP_CORK, &m_corkState, sizeof(m_corkState)) != 0)
        {
            LOG_WARN("Error setting TCP_CORK for fd: {}, will ignore error and continue, errno: {}:{}",
                m_socketFd, errno, strerror(errno));
        }
    }

    return write(m_socketFd, buffer, size);
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::WriteFinal(char* buffer, int size)
{
    return write(m_socketFd, buffer, size);
}
//---------------------------------------------------------------------------------------------------------------------
bool SocketConnection::Sendfile(int fileFd, uint64_t fileStartOffset, uint64_t totalLength)
{
    // loop to serve entire file
    uint64_t totalWritten = 0;
    while (totalWritten < totalLength)
    {
        off_t startOffset = fileStartOffset + totalWritten;
        ssize_t written = sendfile(m_socketFd, fileFd, &startOffset, totalLength - totalWritten);
        if (written <= 0)
        {
            LOG_WARN("Error during sendfile fileFd: {}, errno: {}:{}. total bytes written: {}",
                fileFd, errno, strerror(errno), totalWritten);
            return false;
        }
        if (m_corkState == 1)
        {
            m_corkState = 0;
            if (setsockopt(m_socketFd, IPPROTO_TCP, TCP_CORK, &m_corkState, sizeof(m_corkState)) != 0)
            {
                LOG_WARN("Error re-setting TCP_CORK, will ignore error and continue, errno: {}:{}",
                    errno, strerror(errno));
            }
        }

        totalWritten += written;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::GetId()
{
    return m_socketFd;
}
//---------------------------------------------------------------------------------------------------------------------