//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ConnectionManagerTests.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <cstring>
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
        false,                  // bAuthEnabled
        "",                     // strCacheAccountName
        "",                     // strCacheAccountKey
        maxConnectionsReuseSize,// nMaxConnectionsReuseSize
        reuseConnections,       // bReuseServerConnections
        false,                  // bUseInfiniband
        enableDiscovery,        // bEnableCacheServerDiscovery
        2,                      // nCacheServerDiscoveryIntervalInSeconds
        "",                     // cacheDiscoveryEndpoint
        false                   // bEnableConsistentHashing
    );
}
//---------------------------------------------------------------------------------------------------------------------
int ConnectionManagerTests::StartMockServer(uint16_t port, bool acceptConnections)
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
    ASSERT_EQ(servers.size(), 1);
    EXPECT_EQ(servers[0], "127.0.0.1");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConnectionManagerTests, InitializationWithMultipleServers)
{
    std::vector<std::string> servers = {"server1.test.local", "server2.test.local", "server3.test.local"};
    auto connManager = CreateConnectionManager(servers);
    
    auto actualServers = connManager->GetAllCacheServerHostnames();
    ASSERT_EQ(actualServers.size(), 3);
    
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
        false, "", "", 10, true, false, false, 2, "", false
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
