// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ConnectionManagerTests.h"
#include "gtest/gtest.h"
#include "CacheClient.h"
#include "SocketConnection.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <sys/select.h>

#include "CommonLogger.h"
#include "CacheUtils.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
// AcceptThreadHelper Implementation
//---------------------------------------------------------------------------------------------------------------------
AcceptThreadHelper::AcceptThreadHelper(int serverFd, int connectionHoldTimeMs)
    : m_serverFd(serverFd)
    , m_connectionHoldTimeMs(connectionHoldTimeMs)
{
}
//---------------------------------------------------------------------------------------------------------------------
AcceptThreadHelper::~AcceptThreadHelper()
{
    Stop();
}
//---------------------------------------------------------------------------------------------------------------------
void AcceptThreadHelper::Start()
{
    m_stopAccepting.store(false);
    m_thread = std::thread([this]() {
        while (!m_stopAccepting.load())
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(m_serverFd, &readfds);
            
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms timeout for responsive shutdown
            
            int ret = select(m_serverFd + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(m_serverFd, &readfds))
            {
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                int clientFd = accept(m_serverFd, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientFd >= 0)
                {
                    m_acceptCount++;
                    {
                        std::lock_guard<std::mutex> lock(m_clientFdsMutex);
                        m_clientFds.push_back(clientFd);
                    }
                    
                    if (m_connectionHoldTimeMs > 0)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_connectionHoldTimeMs));
                    }
                }
            }
        }
        
        // Clean up client connections
        std::lock_guard<std::mutex> lock(m_clientFdsMutex);
        for (int fd : m_clientFds)
        {
            close(fd);
        }
        m_clientFds.clear();
    });
}
//---------------------------------------------------------------------------------------------------------------------
void AcceptThreadHelper::Stop()
{
    m_stopAccepting.store(true);
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}
//---------------------------------------------------------------------------------------------------------------------
// ConnectionManagerTests Implementation
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManagerTests::SetUp()
{
    m_mockServerFds.clear();
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManagerTests::TearDown()
{
   for (int fd : m_mockServerFds)
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }
    m_mockServerFds.clear();
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<ConnectionManager> ConnectionManagerTests::CreateConnectionManager(
    const std::vector<std::string>& servers,
    bool useLocalServer,
    bool reuseConnections,
    bool enableDiscovery,
    uint32_t maxConnectionsReuseSize)
{
    return std::make_unique<ConnectionManager>(
        TEST_PORT,              // serverPort
        useLocalServer,         // useLocalServer
        servers,                // cacheServers
        maxConnectionsReuseSize,// nMaxConnectionsReuseSize
        reuseConnections,       // bReuseServerConnections
        false,                  // bUseInfiniband
        enableDiscovery,        // bEnableCacheServerDiscovery
        2,                      // nCacheServerDiscoveryIntervalInSeconds
        "",                     // cacheDiscoveryEndpoint
        false,                  // bEnableConsistentHashing
        DEFAULT_SOCKET_BUF_SIZE,// nSocketSndbufSize
        DEFAULT_SOCKET_BUF_SIZE // nSocketRcvbufSize
    );
}
//---------------------------------------------------------------------------------------------------------------------
int ConnectionManagerTests::StartMockServer(uint16_t port, bool)
{
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        return -1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        close(serverFd);
        return -1;
    }

    if (listen(serverFd, 5) < 0)
    {
        close(serverFd);
        return -1;
    }

    m_mockServerFds.push_back(serverFd);
    return serverFd;
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManagerTests::StopMockServer(int serverFd)
{
    if (serverFd >= 0)
    {
        close(serverFd);
        auto it = std::find(m_mockServerFds.begin(), m_mockServerFds.end(), serverFd);
        if (it != m_mockServerFds.end())
        {
            m_mockServerFds.erase(it);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<AcceptThreadHelper> ConnectionManagerTests::StartAcceptThread(int serverFd, int connectionHoldTimeMs)
{
    auto helper = std::make_unique<AcceptThreadHelper>(serverFd, connectionHoldTimeMs);
    helper->Start();
    return helper;
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Basic ConnectionManager initialization
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, InitializationWithLocalServer)
{
    auto connManager = CreateConnectionManager({}, true);
    
    auto servers = connManager->GetAllCacheServerHostnames();
    ASSERT_EQ(servers.size(), 1U);
    EXPECT_EQ(servers[0], "127.0.0.1");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, InitializationWithMultipleServers)
{
    std::vector<std::string> servers = {"server1.test.local", "server2.test.local", "server3.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    auto actualServers = connManager->GetAllCacheServerHostnames();
    ASSERT_EQ(actualServers.size(), 3U);
    
    // Servers should be sorted (due to set conversion in UpdateCacheServers)
    std::sort(servers.begin(), servers.end());
    EXPECT_EQ(actualServers, servers);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, InitializationWithEmptyServersThrows)
{
    EXPECT_THROW(
        CreateConnectionManager({}, false, true, false),
        const char*
    );
}
//---------------------------------------------------------------------------------------------------------------------
// Test: ConnectionManager shutdown
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, DiscoveryThreadExitsPromptlyOnShutdown)
{
    // Verify that the discovery thread exits promptly when the ConnectionManager is
    // destroyed, instead of blocking for the full discovery interval.
    // Use a long discovery interval to make the test meaningful. If the thread is
    // not woken by the condition variable, this test will take 60+ seconds.
    auto connManager = std::make_unique<ConnectionManager>(
        TEST_PORT,
        false,                  // useLocalServer
        std::vector<std::string>{LOCALHOST},
        10,                     // nMaxConnectionsReuseSize
        true,                   // bReuseServerConnections
        false,                  // bUseInfiniband
        true,                   // bEnableCacheServerDiscovery
        60,                     // nCacheServerDiscoveryIntervalInSeconds
        LOCALHOST,              // cacheDiscoveryEndpoint
        false,                  // bEnableConsistentHashing
        DEFAULT_SOCKET_BUF_SIZE,// nSocketSndbufSize
        DEFAULT_SOCKET_BUF_SIZE // nSocketRcvbufSize
    );

    // Give the discovery thread time to enter the wait_for() call.
    // Without this, a fast CPU can destroy the object before the thread waits,
    // masking the bug (observed on amd64 CI where the test passed in 8ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto start = std::chrono::steady_clock::now();
    connManager.reset(); // triggers destructor → join
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Destructor should complete in well under 5 seconds, not 60.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 5);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: GetCacheServerHostname
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, GetCacheServerHostnameReturnsConsistentServer)
{
    std::vector<std::string> servers = {"server1.test.local", "server2.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    std::string testKey = "test-cache-key-12345";
    
    // Should return same server for same key
    std::string server1 = connManager->GetCacheServerHostname(testKey);
    std::string server2 = connManager->GetCacheServerHostname(testKey);
    
    EXPECT_EQ(server1, server2);
    EXPECT_FALSE(server1.empty());
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Connection timeout handling
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionToUnreachableHostTimesOut)
{
    std::vector<std::string> servers = {UNREACHABLE_HOST};
    auto connManager = CreateConnectionManager(servers);
    
    auto startTime = std::chrono::steady_clock::now();
    auto connection = connManager->GetConnection(UNREACHABLE_HOST);
    auto endTime = std::chrono::steady_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    
    // Connection should fail
    EXPECT_EQ(connection, nullptr);
    
    // Should timeout around 5 seconds (the configured timeout in CreateSocketAndConnect)
    // Allow some tolerance for system variance
    EXPECT_GE(elapsed.count(), 4) << "Connection should wait for timeout";
    EXPECT_LE(elapsed.count(), 8) << "Connection timeout should be bounded";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionToNonListeningPortFails)
{
    // Use localhost with a port that nothing is listening on
    std::vector<std::string> servers = {LOCALHOST};
    
    // Create manager with a port that has no server
    auto connManager = std::make_unique<ConnectionManager>(
        59999,  // Port with no server
        false,
        servers,
        10, true, false, false, 2, "", false,
        DEFAULT_SOCKET_BUF_SIZE, DEFAULT_SOCKET_BUF_SIZE
    );
    
    auto connection = connManager->GetConnection(LOCALHOST);
    
    // Should fail quickly since connection is refused
    EXPECT_EQ(connection, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, SuccessfulConnectionToMockServer)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0) << "Failed to start mock server";
    
    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(servers);
    
    // Start accept thread
    auto acceptHelper = StartAcceptThread(serverFd, 500);
    
    auto connection = connManager->GetConnection(LOCALHOST);
    
    // Connection should succeed
    EXPECT_NE(connection, nullptr);
    if (connection)
    {
        EXPECT_TRUE(connection->IsValidConnection());
    }
    
    acceptHelper->Stop();
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Connection pooling
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionPoolingReuseEnabled)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0) << "Failed to start mock server";
    
    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(servers, false, true /* reuseConnections */);
    
    // Keep connection alive longer to allow pooling test
    auto acceptHelper = StartAcceptThread(serverFd, 2000);
    
    // Get a connection
    auto connection1 = connManager->GetConnection(LOCALHOST);
    ASSERT_NE(connection1, nullptr);
    
    // Release it back to the pool
    connManager->ReleaseConnection(LOCALHOST, std::move(connection1), false);
    
    // Wait for accept thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Get another connection - should reuse pooled connection
    auto connection2 = connManager->GetConnection(LOCALHOST);
    EXPECT_NE(connection2, nullptr);
    
    acceptHelper->Stop();
    
    // Only one actual connection should have been made
    EXPECT_EQ(acceptHelper->GetAcceptCount(), 1) << "Connection pooling should reuse connections";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionPoolingDisabled)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0) << "Failed to start mock server";
    
    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(servers, false, false /* reuseConnections disabled */);
    
    auto acceptHelper = StartAcceptThread(serverFd, 100);
    
    auto connection1 = connManager->GetConnection(LOCALHOST);
    ASSERT_NE(connection1, nullptr);
    
    // Release doesn't pool when disabled
    connManager->ReleaseConnection(LOCALHOST, std::move(connection1), false);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Get another connection - should create new one
    auto connection2 = connManager->GetConnection(LOCALHOST);
    EXPECT_NE(connection2, nullptr);
    
    acceptHelper->Stop();
    
    // Two connections should have been made
    EXPECT_EQ(acceptHelper->GetAcceptCount(), 2) << "Without pooling, each GetConnection should create new connection";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ErrorConnectionNotPooled)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0);
    
    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(servers, false, true);
    
    auto acceptHelper = StartAcceptThread(serverFd, 100);
    
    auto connection1 = connManager->GetConnection(LOCALHOST);
    ASSERT_NE(connection1, nullptr);
    
    // Release with error flag - should not pool
    connManager->ReleaseConnection(LOCALHOST, std::move(connection1), true /* errorConnection */);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto connection2 = connManager->GetConnection(LOCALHOST);
    EXPECT_NE(connection2, nullptr);
    
    acceptHelper->Stop();
    
    // Two connections made because error connection wasn't pooled
    EXPECT_EQ(acceptHelper->GetAcceptCount(), 2);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: GetConnection with invalid server
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, GetConnectionWithInvalidServer)
{
    std::vector<std::string> servers = {"server1.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    // Try to get connection for server not in the list
    auto connection = connManager->GetConnection("invalid-server.test.local");
    
    EXPECT_EQ(connection, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, GetConnectionWithEmptyServerName)
{
    std::vector<std::string> servers = {"server1.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    auto connection = connManager->GetConnection("");
    
    EXPECT_EQ(connection, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Thread safety - concurrent GetConnection calls
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConcurrentGetConnectionCalls)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0);
    
    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(
        servers,
        false /* useLocalServer */,
        true /* reuseConnections */,
        false /* enableDiscovery */,
        20 /* maxConnectionsReuseSize */);
    
    auto acceptHelper = StartAcceptThread(serverFd, 500);
    
    const int numThreads = 5;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    
    for (int i = 0; i < numThreads; i++)
    {
        threads.emplace_back([&connManager, &successCount, &failCount]() {
            auto conn = connManager->GetConnection(LOCALHOST);
            if (conn != nullptr)
            {
                successCount++;
                connManager->ReleaseConnection(LOCALHOST, std::move(conn), false);
            }
            else
            {
                failCount++;
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    acceptHelper->Stop();
    
    // All threads should have completed without crashing
    EXPECT_EQ(successCount.load() + failCount.load(), numThreads);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: GetAllCacheServerHostnames returns copy
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, GetAllCacheServerHostnamesReturnsCopy)
{
    std::vector<std::string> servers = {"server1.test.local", "server2.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    auto hostnames1 = connManager->GetAllCacheServerHostnames();
    auto hostnames2 = connManager->GetAllCacheServerHostnames();
    
    EXPECT_EQ(hostnames1.size(), hostnames2.size());
    EXPECT_EQ(hostnames1, hostnames2);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Connection timeout with slow server
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionTimeoutWithSlowAccept)
{
    // This test verifies the 5-second timeout in CreateSocketAndConnect
    // by connecting to an address that won't respond (simulating network issues)
    
    std::vector<std::string> servers = {UNREACHABLE_HOST};
    auto connManager = CreateConnectionManager(servers);
    
    auto startTime = std::chrono::steady_clock::now();
    auto connection = connManager->GetConnection(UNREACHABLE_HOST);
    auto endTime = std::chrono::steady_clock::now();
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    EXPECT_EQ(connection, nullptr) << "Connection to unreachable host should fail";
    
    // The timeout is set to 5 seconds in the code
    // Allow some tolerance (4-7 seconds)
    EXPECT_GE(elapsed.count(), 4000) << "Should wait for timeout";
    EXPECT_LE(elapsed.count(), 7000) << "Should not wait much longer than timeout";
}
//---------------------------------------------------------------------------------------------------------------------
// Test: DNS resolution failure
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionToInvalidHostnameFails)
{
    std::vector<std::string> servers = {"invalid.hostname.that.does.not.exist.test"};
    auto connManager = CreateConnectionManager(servers);
    
    auto connection = connManager->GetConnection("invalid.hostname.that.does.not.exist.test");
    
    // Should fail due to DNS resolution failure
    EXPECT_EQ(connection, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
// Test: WaitForSocketWritable uses poll(), which works for fd >= FD_SETSIZE
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, WaitForSocketWritableWorksAboveFdSetSize)
{
    // Regression test for the select() → poll() fix to handle high FD numbers in CreateSocketAndConnect.
    //
    // select() + FD_SET/FD_ISSET have undefined behaviour for fd >= FD_SETSIZE (1024):
    // FD_SET silently writes past the fd_set bitmap, so FD_ISSET returns false and
    // the connection attempt is wrongly treated as failed.
    // poll() stores the fd directly in struct pollfd — no FD_SETSIZE limit.
    //
    // To bypass the connect() fast-path (loopback connect() always returns 0
    // synchronously, never EINPROGRESS), we test WaitForSocketWritable() directly
    // with a socketpair() whose fd has been pushed above FD_SETSIZE.
    // A socketpair is already connected so it is immediately writable: poll(POLLOUT)
    // returns it as ready, but the old FD_ISSET may return false for fd >= FD_SETSIZE.

    // Open /dev/null dummies to exhaust fd numbers up to FD_SETSIZE.
    std::vector<int> dummyFds;
    for (;;)
    {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            break;
        dummyFds.push_back(fd);
        if (fd >= FD_SETSIZE)
            break;
    }

    const bool aboveFdSetSize = !dummyFds.empty() && dummyFds.back() >= FD_SETSIZE;

    // Create a connected socket pair so the first fd is writable immediately.
    int spFds[2] = {-1, -1};
    const bool pairOk = (socketpair(AF_UNIX, SOCK_STREAM, 0, spFds) == 0);

    // Release dummies now so they don't starve the rest of the process.
    for (int fd : dummyFds)
        close(fd);

    if (!aboveFdSetSize)
    {
        if (pairOk) { close(spFds[0]); close(spFds[1]); }
        GTEST_SKIP() << "Could not push an fd to >= FD_SETSIZE; cannot test high-fd path";
    }

    ASSERT_TRUE(pairOk) << "socketpair() failed: " << strerror(errno);

    // spFds[0] was allocated after dummyFds, so it should be >= FD_SETSIZE.
    const int highFd = spFds[0];
    ASSERT_GE(highFd, FD_SETSIZE)
        << "socketpair fd " << highFd << " is below FD_SETSIZE; test setup failed";

    // Note: we do NOT assert FD_ISSET behaviour here. For fd >= FD_SETSIZE, FD_SET
    // writes past the end of the fd_set bitmap — this is undefined behaviour. The
    // result of FD_ISSET is unpredictable (it may return true or false depending on
    // what happens to be in that memory). The only safe fix is to not use select()
    // at all for arbitrary fd numbers, which is what WaitForSocketWritable does.

    // WaitForSocketWritable (poll-based) must correctly report the socket as writable.
    EXPECT_TRUE(ConnectionManager::WaitForSocketWritable(highFd, 1000))
        << "poll()-based WaitForSocketWritable must return true for fd=" << highFd
        << " >= FD_SETSIZE; the old select() code returned false here";

    close(spFds[0]);
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionSucceedsWithHighFdNumber)
{
    // Regression test for the select() → poll() fix to handle high FD numbers in CreateSocketAndConnect.
    //
    // select() + FD_SET/FD_ISSET have undefined behaviour for fd >= FD_SETSIZE (1024).
    // poll() has no such limit.
    //
    // Strategy: exhaust fd numbers up to FD_SETSIZE by opening /dev/null dummies so
    // that the next socket() call returns an fd >= FD_SETSIZE, then verify the
    // connection still succeeds.

    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0) << "Failed to start mock server";

    // Open dummy fds until (at least) the next allocation would be >= FD_SETSIZE.
    std::vector<int> dummyFds;
    for (;;)
    {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            break; // hit the process fd limit before FD_SETSIZE — see skip below
        dummyFds.push_back(fd);
        if (fd >= FD_SETSIZE + 5) // give some buffer above FD_SETSIZE to be sure
            break;
    }

    bool aboveFdSetSize = !dummyFds.empty() && dummyFds.back() >= FD_SETSIZE;

    auto acceptHelper = StartAcceptThread(serverFd, 500);

    std::vector<std::string> servers = {LOCALHOST};
    auto connManager = CreateConnectionManager(servers);
    auto connection = connManager->GetConnection(LOCALHOST);

    // Release dummy fds before asserting so we don't starve subsequent tests.
    for (int fd : dummyFds)
        close(fd);

    if (!aboveFdSetSize)
    {
        acceptHelper->Stop();
        GTEST_SKIP() << "Process fd limit reached before FD_SETSIZE; cannot test high-fd path";
    }

    EXPECT_NE(connection, nullptr)
        << "poll()-based connect must work with fd >= FD_SETSIZE; "
           "the old select() code would invoke undefined behaviour above fd 1024";
    if (connection)
    {
        // IsValidConnection() does recv(MSG_PEEK|MSG_DONTWAIT) which returns 0 (EOF)
        // once the server closes the socket. Must check before stopping the accept helper.
        EXPECT_TRUE(connection->IsValidConnection());
        EXPECT_GE(connection->GetId(), FD_SETSIZE) << "Connection fd should be above FD_SETSIZE";
    }

    acceptHelper->Stop();
}
//---------------------------------------------------------------------------------------------------------------------
// Test: Connection pool size limits
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ConnectionPoolSizeLimit)
{
    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0);
    
    std::vector<std::string> servers = {LOCALHOST};
    // Set max pool size to 2
    auto connManager = CreateConnectionManager(servers, false, true, false, 2);
    
    auto acceptHelper = StartAcceptThread(serverFd, 50);
    
    // Get and release more connections than the pool can hold
    std::vector<std::unique_ptr<IConnection>> connections;
    for (int i = 0; i < 5; i++)
    {
        auto conn = connManager->GetConnection(LOCALHOST);
        if (conn)
        {
            connections.push_back(std::move(conn));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Release all connections
    for (auto& conn : connections)
    {
        connManager->ReleaseConnection(LOCALHOST, std::move(conn), false);
    }
    connections.clear();
    
    acceptHelper->Stop();
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, IsIbZeroCopyEnabledRespectsEnvVar)
{
    // Disabled by default when the env var is unset.
    unsetenv("DACS_ENABLE_IB_ZERO_COPY");
    EXPECT_FALSE(ConnectionManager::IsIbZeroCopyEnabled());

    // Any value other than "true" stays disabled.
    setenv("DACS_ENABLE_IB_ZERO_COPY", "false", 1);
    EXPECT_FALSE(ConnectionManager::IsIbZeroCopyEnabled());

    // Only "true" (case-insensitive) enables it.
    setenv("DACS_ENABLE_IB_ZERO_COPY", "TRUE", 1);
    EXPECT_TRUE(ConnectionManager::IsIbZeroCopyEnabled());
    setenv("DACS_ENABLE_IB_ZERO_COPY", "true", 1);
    EXPECT_TRUE(ConnectionManager::IsIbZeroCopyEnabled());

    unsetenv("DACS_ENABLE_IB_ZERO_COPY");
}
//---------------------------------------------------------------------------------------------------------------------
// Test: SO_RCVTIMEO / SO_SNDTIMEO are set correctly on the socket
//---------------------------------------------------------------------------------------------------------------------
// RAII guard to temporarily lower SocketConnection::socketTimeoutMillisMin for faster tests.
struct ScopedMinTimeout
{
    explicit ScopedMinTimeout(uint32_t tempValue)
        : m_saved(SocketConnection::socketTimeoutMillisMin)
    {
        SocketConnection::socketTimeoutMillisMin = tempValue;
    }
    ~ScopedMinTimeout() { SocketConnection::socketTimeoutMillisMin = m_saved; }
private:
    uint32_t m_saved;
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, ReceiveTimesOutWhenServerSendsNoData)
{
    // Lower the minimum timeout so the test runs in ~500 ms instead of 15 s.
    ScopedMinTimeout scopedTimeout(500);

    int serverFd = StartMockServer(TEST_PORT);
    ASSERT_GE(serverFd, 0);
    // Hold the connection open (without sending) for longer than the recv timeout.
    auto acceptHelper = StartAcceptThread(serverFd, 3000);

    auto connManager = CreateConnectionManager({LOCALHOST});
    auto connection = connManager->GetConnection(LOCALHOST);
    ASSERT_NE(connection, nullptr);

    char buf[64];
    auto start = std::chrono::steady_clock::now();
    int result = connection->Receive(buf, sizeof(buf));
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LT(result, 0) << "Receive should time out when server sends no data";
    EXPECT_GE(elapsedMs, 400) << "Should wait approximately the configured timeout";
    EXPECT_LE(elapsedMs, 2000) << "Should not block much beyond the timeout";

    acceptHelper->Stop();
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, RecvTimeoutSocketOptionIsSetAfterReceive)
{
    // Verify SO_RCVTIMEO is set to GetTimeoutMillis(N) on the socket after Receive(buf, N).
    int spFds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, spFds), 0);

    const size_t readSize = 64;
    const int expectedMs = SocketConnection::GetTimeoutMillis(readSize);

    // Pre-load data so recv() returns immediately.
    char sendBuf[readSize] = {};
    ASSERT_EQ((int)write(spFds[1], sendBuf, readSize), (int)readSize);

    {
        SocketConnection conn(spFds[0]);
        char recvBuf[readSize];
        conn.Receive(recvBuf, readSize);

        struct timeval tv{};
        socklen_t len = sizeof(tv);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
        int actualMs = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
        EXPECT_EQ(actualMs, expectedMs) << "SO_RCVTIMEO should match GetTimeoutMillis(readSize)";
    } // SocketConnection destructor closes spFds[0]
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, RecvTimeoutSocketOptionUpdatedForDifferentSize)
{
    // Verify SO_RCVTIMEO is updated when the same connection is reused with a different IO size.
    int spFds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, spFds), 0);

    const size_t smallSize = 64;
    const size_t largeSize = 10 * 1024 * 1024; // 10 MB
    const int smallMs = SocketConnection::GetTimeoutMillis(smallSize);
    const int largeMs = SocketConnection::GetTimeoutMillis(largeSize);
    ASSERT_NE(smallMs, largeMs) << "Test requires two sizes that produce different timeouts";

    // Pre-load data for both reads. The large read only needs 1 byte so that poll()
    // returns POLLIN and setsockopt(SO_RCVTIMEO) is reached inside TimedRead before
    // recv() returns. Without any data, poll() would time out and return early before
    // the setsockopt call, leaving SO_RCVTIMEO unchanged.
    char sendBuf[smallSize + 1] = {};
    ASSERT_EQ((int)write(spFds[1], sendBuf, smallSize + 1), (int)(smallSize + 1));

    {
        SocketConnection conn(spFds[0]);
        char recvBuf[smallSize];

        // First read with small size: SO_RCVTIMEO should be set to smallMs.
        conn.Receive(recvBuf, smallSize);

        struct timeval tvAfterSmall{};
        socklen_t len = sizeof(tvAfterSmall);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tvAfterSmall, &len), 0);
        const int actualSmallMs = (int)(tvAfterSmall.tv_sec * 1000 + tvAfterSmall.tv_usec / 1000);
        EXPECT_EQ(actualSmallMs, smallMs)
            << "SO_RCVTIMEO should be set to GetTimeoutMillis(smallSize) after first receive";

        // Second read requesting largeSize bytes: only 1 byte is available, so recv()
        // returns 1. That is fine — we only care that setsockopt(SO_RCVTIMEO) was called.
        char largeBuf[1];
        conn.Receive(largeBuf, largeSize);

        struct timeval tvAfterLarge{};
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tvAfterLarge, &len), 0);
        const int actualLargeMs = (int)(tvAfterLarge.tv_sec * 1000 + tvAfterLarge.tv_usec / 1000);
        EXPECT_EQ(actualLargeMs, largeMs)
            << "SO_RCVTIMEO must be updated when IO size changes between receives";
    }
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, RequestBytesWrittenControlsReceiveTimeout)
{
    int spFds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, spFds), 0);

    constexpr size_t headerSize = 4;
    constexpr uint64_t mediumUploadSize = 10ULL * 1024 * 1024;
    constexpr uint64_t largeUploadSize = 4ULL * 1024 * 1024 * 1024;
    const int mediumTimeoutMs = SocketConnection::GetTimeoutMillis(mediumUploadSize);
    const int largeTimeoutMs = SocketConnection::GetTimeoutMillis(largeUploadSize);
    const int defaultTimeoutMs = SocketConnection::GetTimeoutMillis(headerSize);

    char responseData[headerSize * 3] = {};
    ASSERT_EQ((int)write(spFds[1], responseData, sizeof(responseData)), (int)sizeof(responseData));

    {
        SocketConnection conn(spFds[0]);
        char recvBuf[headerSize];
        struct timeval tv{};
        socklen_t len = sizeof(tv);

        char requestData[headerSize] = {};
        ASSERT_EQ(conn.Send(requestData, headerSize), (int)headerSize);
        ASSERT_EQ(conn.Send(requestData, headerSize), (int)headerSize);
        EXPECT_EQ(conn.m_requestBytesWritten, headerSize * 2);

        conn.m_requestBytesWritten = mediumUploadSize;
        ASSERT_EQ(conn.Receive(recvBuf, headerSize), (int)headerSize);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
        EXPECT_EQ((int)(tv.tv_sec * 1000 + tv.tv_usec / 1000), mediumTimeoutMs);
        EXPECT_EQ(mediumTimeoutMs, 20000);

        conn.m_requestBytesWritten = largeUploadSize;
        ASSERT_EQ(conn.Receive(recvBuf, headerSize), (int)headerSize);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
        EXPECT_EQ((int)(tv.tv_sec * 1000 + tv.tv_usec / 1000), largeTimeoutMs);
        EXPECT_EQ(largeTimeoutMs, 120000);

        conn.ResetConnection();
        ASSERT_EQ(conn.Receive(recvBuf, headerSize), (int)headerSize);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
        EXPECT_EQ((int)(tv.tv_sec * 1000 + tv.tv_usec / 1000), defaultTimeoutMs);
        EXPECT_EQ(defaultTimeoutMs, 15000);
    }
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, SndTimeoutSocketOptionIsSetAfterSend)
{
    // Verify SO_SNDTIMEO is set to GetTimeoutMillis(N) on the socket after Send(buf, N).
    int spFds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, spFds), 0);

    const size_t writeSize = 64;
    const int expectedMs = SocketConnection::GetTimeoutMillis(writeSize);

    {
        SocketConnection conn(spFds[0]);
        char buf[writeSize] = {};
        conn.Send(buf, writeSize);

        struct timeval tv{};
        socklen_t len = sizeof(tv);
        ASSERT_EQ(getsockopt(spFds[0], SOL_SOCKET, SO_SNDTIMEO, &tv, &len), 0);
        int actualMs = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
        EXPECT_EQ(actualMs, expectedMs) << "SO_SNDTIMEO should match GetTimeoutMillis(writeSize)";
    }
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, SendTimesOutWhenReceiveBufferFull)
{
    // Verify that Send() returns early (bytes written < requested) when the socket
    // send buffer is full and no one is consuming data from the other end.
    // The timeout comes from poll(POLLOUT, timeoutMillis) inside TimedWrite.
    ScopedMinTimeout scopedTimeout(500);

    int spFds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, spFds), 0);

    // Shrink both sides' buffers so the send buffer is easy to fill.
    const int bufSize = 4096;
    setsockopt(spFds[0], SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize));
    setsockopt(spFds[1], SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));

    // Fill the send buffer with non-blocking writes until the kernel signals EAGAIN.
    const int origFlags = fcntl(spFds[0], F_GETFL, 0);
    ASSERT_GE(origFlags, 0);
    ASSERT_EQ(fcntl(spFds[0], F_SETFL, origFlags | O_NONBLOCK), 0);

    char fillBuf[4096] = {};
    while (write(spFds[0], fillBuf, sizeof(fillBuf)) > 0) {}
    ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK)
        << "Expected send buffer to be full after non-blocking writes";

    // Restore blocking mode — SocketConnection expects a blocking socket.
    ASSERT_EQ(fcntl(spFds[0], F_SETFL, origFlags), 0);

    {
        SocketConnection conn(spFds[0]);  // takes ownership of spFds[0]
        char sendData[64] = {};

        auto start = std::chrono::steady_clock::now();
        int result = conn.Send(sendData, sizeof(sendData));
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        // Send could not write all bytes because the buffer was full and timed out.
        EXPECT_LT(result, (int)sizeof(sendData)) << "Send should not complete when buffer is full";
        EXPECT_GE(elapsedMs, 400) << "Should wait approximately the configured timeout";
        EXPECT_LE(elapsedMs, 2000) << "Should not block much beyond the timeout";
    }  // SocketConnection destructor closes spFds[0]
    close(spFds[1]);
}
//---------------------------------------------------------------------------------------------------------------------
