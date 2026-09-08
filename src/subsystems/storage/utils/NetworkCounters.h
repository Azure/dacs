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
//
//---------------------------------------------------------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace NetworkCounterConstants
{
    const char* const TcpSection = "Tcp";
    const char* const TcpExtSection = "TcpExt";
    const char* const ProcNetSnmpPath = "/proc/net/snmp";
    const char* const ProcFileNrPath = "/proc/sys/fs/file-nr";
    const char* const ProcNetNetstatPath = "/proc/net/netstat";
}

struct NetworkCounters
{
    // Number of times a connection could not be accepted because the accept queue was full.
    uint64_t listenOverflows = 0;
    // Number of incoming connection requests dropped for a listening socket (including
    // accept queue overflows and other causes like memory pressure or SYN queue issues).
    uint64_t listenDrops = 0;
    // Number of inbound TCP connection requests dropped because the server's SYN queue has reached
    // its maximum capacity
    uint64_t synQueueFullDrops = 0;
    // Number of times the server issued a SYN cookie instead of allocating
    // normal kernel state for a half-open connection.
    uint64_t synCookiesSent = 0;
    // Number of valid SYN cookies received back, allowing the connection
    // handshake to proceed.
    uint64_t syncookiesRecv = 0;
    // Total number of inbound TCP connections accepted (passive opens).
    uint64_t passiveOpens = 0;
    // Total number of outbound TCP connection attempts initiated (active opens).
    uint64_t activeOpens = 0;
    // Current count of allocated/open file descriptors in the system.
    uint64_t openFdCount = 0;
};

bool ReadTcpOpenCounters(uint64_t& passiveOpens, uint64_t& activeOpens);
bool ReadOpenFdCount(uint64_t& openFdCount);
bool ReadNetstatCounters(NetworkCounters& counters);
//---------------------------------------------------------------------------------------------------------------------