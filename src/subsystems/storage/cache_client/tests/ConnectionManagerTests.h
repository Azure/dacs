//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>

#include "ConnectionManager.h"
//---------------------------------------------------------------------------------------------------------------------
/**
 * AcceptThreadHelper - RAII helper class to manage accept threads in tests
 * 
 * This class provides a clean way to start and stop accept threads that handle
 * incoming connections on a mock server socket. It supports:
 * - Counting accepted connections
 * - Custom connection hold times
 * - Automatic cleanup on destruction
 */
class AcceptThreadHelper
{
public:
    AcceptThreadHelper(int serverFd, int connectionHoldTimeMs = 500);
    ~AcceptThreadHelper();

    // Start accepting connections in a background thread
    void Start();
    
    // Stop accepting and join the thread
    void Stop();
    
    // Get the number of connections accepted
    int GetAcceptCount() const { return m_acceptCount.load(); }

private:
    int m_serverFd;
    int m_connectionHoldTimeMs;
    std::atomic<int> m_acceptCount{0};
    std::atomic<bool> m_stopAccepting{false};
    std::thread m_thread;
    std::vector<int> m_clientFds;
    std::mutex m_clientFdsMutex;
};
//---------------------------------------------------------------------------------------------------------------------
/**
 * ConnectionManagerTests - Unit tests for the ConnectionManager class
 * 
 * These tests cover:
 * - Connection creation and management
 * - Connection pooling and reuse
 * - Connection timeout handling
 * - Cache server discovery
 * - Thread safety
 * - Error handling scenarios
 */
class ConnectionManagerTests : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    // Helper to create a ConnectionManager with default test settings
    std::unique_ptr<ConnectionManager> CreateConnectionManager(
        const std::vector<std::string>& servers,
        bool useLocalServer = false,
        bool reuseConnections = true,
        bool enableDiscovery = false,
        uint32_t maxConnectionsReuseSize = 10);

    // Helper to start a mock server that accepts connections
    int StartMockServer(uint16_t port, bool acceptConnections = true);
    
    // Helper to start a mock server that delays accepting connections (for timeout tests)
    int StartDelayedMockServer(uint16_t port, int delaySeconds);
    
    // Helper to stop a mock server
    void StopMockServer(int serverFd);
    
    // Helper to create and start an accept thread for a server socket
    std::unique_ptr<AcceptThreadHelper> StartAcceptThread(int serverFd, int connectionHoldTimeMs = 500);

    // Test constants
    static constexpr uint16_t TEST_PORT = 19065;
    static constexpr uint16_t TIMEOUT_TEST_PORT = 19066;
    static constexpr const char* LOCALHOST = "127.0.0.1";
    static constexpr const char* UNREACHABLE_HOST = "192.0.2.1";  // RFC5737 test address
    static constexpr int CONNECTION_TIMEOUT_SECONDS = 5;

    // Track mock server file descriptors for cleanup
    std::vector<int> m_mockServerFds;
};
//---------------------------------------------------------------------------------------------------------------------
