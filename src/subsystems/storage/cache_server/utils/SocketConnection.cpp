///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "SocketConnection.h"
#include "Logger.h"
#include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
#include <algorithm>
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
bool SocketConnection::Recvfile(int fileFd, uint64_t totalLength)
{
    // Use splice() to move data from socket directly to file via a kernel pipe,
    // avoiding user-space copies. This is the upload-side equivalent of Sendfile().
    int pipefd[2];
    if (pipe(pipefd) < 0)
    {
        LOG_WARN("Failed to create pipe for splice, errno: {}:{}", errno, strerror(errno));
        return false;
    }

    // Try to increase pipe buffer to 4 MiB to allow larger kernel-side batch transfers.
    // This is best-effort; the default (64 KiB) still works correctly.
#ifdef F_SETPIPE_SZ
    static constexpr int DESIRED_PIPE_SIZE = 4 * 1024 * 1024;
    if (fcntl(pipefd[1], F_SETPIPE_SZ, DESIRED_PIPE_SIZE) < 0)
    {
        LOG_DEBUG("F_SETPIPE_SZ failed (continuing with default pipe size), errno: {}:{}",
                  errno, strerror(errno));
    }
#endif

    // Cap each splice() call to avoid passing huge len values that exceed ssize_t
    // or cause EINVAL on some kernels. 1 GiB is well within ssize_t and efficient.
    static constexpr size_t MAX_SPLICE_LEN = 1ULL * 1024 * 1024 * 1024;

    uint64_t totalReceived = 0;
    bool success = true;

    while (totalReceived < totalLength)
    {
        size_t remaining = static_cast<size_t>(std::min(totalLength - totalReceived,
                                                        static_cast<uint64_t>(MAX_SPLICE_LEN)));

        // splice: socket -> pipe (kernel-space)
        ssize_t nSpliced = splice(m_socketFd, nullptr, pipefd[1], nullptr,
                                  remaining, SPLICE_F_MOVE | SPLICE_F_MORE);
        if (nSpliced <= 0)
        {
            if (nSpliced < 0 && errno == EINTR)
            {
                continue;
            }
            if (nSpliced == 0)
            {
                LOG_WARN("splice(socket->pipe) unexpected EOF, fd: {}, received: {}/{}",
                         m_socketFd, totalReceived, totalLength);
            }
            else
            {
                LOG_WARN("splice(socket->pipe) failed, fd: {}, errno: {}:{}, received: {}/{}",
                         m_socketFd, errno, strerror(errno), totalReceived, totalLength);
            }
            success = false;
            break;
        }

        // splice: pipe -> file (kernel-space)
        ssize_t nWritten = 0;
        while (nWritten < nSpliced)
        {
            ssize_t w = splice(pipefd[0], nullptr, fileFd, nullptr,
                               nSpliced - nWritten, SPLICE_F_MOVE | SPLICE_F_MORE);
            if (w <= 0)
            {
                if (w < 0 && errno == EINTR)
                {
                    continue;
                }
                if (w == 0)
                {
                    LOG_WARN("splice(pipe->file) unexpected EOF, fileFd: {}, written: {}/{}",
                             fileFd, nWritten, nSpliced);
                }
                else
                {
                    LOG_WARN("splice(pipe->file) failed, fileFd: {}, errno: {}:{}, written: {}/{}",
                             fileFd, errno, strerror(errno), nWritten, nSpliced);
                }
                success = false;
                break;
            }
            nWritten += w;
        }

        if (!success)
        {
            break;
        }

        totalReceived += nSpliced;
    }

    close(pipefd[0]);
    close(pipefd[1]);
    return success;
}
//---------------------------------------------------------------------------------------------------------------------
int SocketConnection::GetId()
{
    return m_socketFd;
}
//---------------------------------------------------------------------------------------------------------------------