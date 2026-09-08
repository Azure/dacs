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
#include "Logger.h"
#include "NetworkCounters.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

//---------------------------------------------------------------------------------------------------------------------
// Parses a single named section from a /proc/net-style network file. Such files are organized as pairs of lines: a
// "<Section>: <name> <name> ..." header line followed by a "<Section>: <value> <value> ..." values line with matching
// columns. Opens the file, locates the requested section, and populates out with the header->value mapping. Returns
// true only if the section was found and its header/value columns parsed successfully.
static bool ParseSectionFromNetworkFile(const std::string& section,
                                    const std::string& networkFilePath,
                                    std::unordered_map<std::string, uint64_t>& out)
{
    LOG_DEBUG("[NetworkCounters] ParseSectionFromNetworkFile(section={}, file={})", section, networkFilePath);

    std::ifstream file(networkFilePath);
    if (!file.is_open())
    {
        LOG_WARN("[NetworkCounters] ParseSectionFromNetworkFile(section={}) failed to open {}", section, networkFilePath);
        return false;
    }

    const std::string prefix = section + ":";
    std::string headerLine;
    std::string valuesLine;

    while (std::getline(file, headerLine))
    {
        // Only process the requested section; skip other sections (e.g. Ip, Udp, IpExt) without warning.
        if (headerLine.rfind(prefix, 0) != 0)
        {
            continue;
        }

        if (!std::getline(file, valuesLine) || valuesLine.rfind(prefix, 0) != 0)
        {
            LOG_WARN("[NetworkCounters] ParseSectionFromNetworkFile(section={}) missing or malformed values line", section);
            return false;
        }

        std::istringstream headerStream(headerLine.substr(prefix.size()));
        std::istringstream valueStream(valuesLine.substr(prefix.size()));
        std::vector<std::string> headers;
        std::vector<uint64_t> values;

        for (std::string token; headerStream >> token;)
        {
            headers.push_back(token);
        }

        for (std::string token; valueStream >> token;)
        {
            try
            {
                values.push_back(std::stoull(token));
            }
            catch (const std::exception&)
            {
                LOG_WARN("[NetworkCounters] ParseSectionFromNetworkFile(section={}) failed to parse value token='{}'",
                         section,
                         token);
                return false;
            }
        }

        if (headers.size() != values.size())
        {
            LOG_WARN("[NetworkCounters] ParseSectionFromNetworkFile(section={}) header/value count mismatch headers={} values={} ",
                     section,
                     headers.size(),
                     values.size());
            return false;
        }

        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            out[headers[i]] = values[i];
        }

        LOG_DEBUG("[NetworkCounters] ParseSectionFromNetworkFile(section={}) parsed {} fields", section, headers.size());
        return true;
    }

    LOG_WARN("[NetworkCounters] ParseSectionFromNetworkFile(section={}) section not found in {}", section, networkFilePath);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
// Parses /proc/net/snmp, which exposes SNMP MIB counters organized as pairs of lines: a "<Section>: <name> <name> ..."
// header line followed by a "<Section>: <value> <value> ..." values line with matching columns. Reads the Tcp section
// and extracts the PassiveOpens (inbound connections accepted) and ActiveOpens (outbound connections initiated) counters.
bool ReadTcpOpenCounters(uint64_t& passiveOpens, uint64_t& activeOpens)
{
    passiveOpens = 0;
    activeOpens = 0;

    LOG_DEBUG("[NetworkCounters] ReadTcpOpenCounters() reading {}", NetworkCounterConstants::ProcNetSnmpPath);

    std::unordered_map<std::string, uint64_t> tcpValues;
    if (!ParseSectionFromNetworkFile(NetworkCounterConstants::TcpSection,
                                     NetworkCounterConstants::ProcNetSnmpPath,
                                     tcpValues))
    {
        LOG_WARN("[NetworkCounters] ReadTcpOpenCounters() failed to parse {} section from {}",
                 NetworkCounterConstants::TcpSection,
                 NetworkCounterConstants::ProcNetSnmpPath);
        return false;
    }

    const auto passiveIt = tcpValues.find("PassiveOpens");
    const auto activeIt = tcpValues.find("ActiveOpens");
    const bool foundPassiveOpens = passiveIt != tcpValues.end();
    const bool foundActiveOpens = activeIt != tcpValues.end();

    if (foundPassiveOpens)
    {
        passiveOpens = passiveIt->second;
    }
    if (foundActiveOpens)
    {
        activeOpens = activeIt->second;
    }

    LOG_DEBUG("[NetworkCounters] ReadTcpOpenCounters() foundPassiveOpens={} passiveOpens={} foundActiveOpens={} activeOpens={}",
             foundPassiveOpens,
             passiveOpens,
             foundActiveOpens,
             activeOpens);
    return foundPassiveOpens && foundActiveOpens;
}
//---------------------------------------------------------------------------------------------------------------------
// Parses /proc/sys/fs/file-nr, a single line of three whitespace-separated values:
// "<allocated/open file handles> <free allocated handles> <system-wide max handles>".
// Returns the first field, i.e. the number of file descriptors currently open across the system.
bool ReadOpenFdCount(uint64_t& openFdCount)
{
    openFdCount = 0;

    LOG_DEBUG("[NetworkCounters] ReadOpenFdCount() reading {}", NetworkCounterConstants::ProcFileNrPath);
    std::ifstream fileNr(NetworkCounterConstants::ProcFileNrPath);
    if (!fileNr.is_open())
    {
        LOG_WARN("[NetworkCounters] ReadOpenFdCount() failed to open {}", NetworkCounterConstants::ProcFileNrPath);
        return false;
    }

    // /proc/sys/fs/file-nr format: <allocated/open> <unused> <max>
    if (!(fileNr >> openFdCount))
    {
        LOG_WARN("[NetworkCounters] ReadOpenFdCount() failed to parse open fd count");
        return false;
    }

    LOG_DEBUG("[NetworkCounters] ReadOpenFdCount() openFdCount={}", openFdCount);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
// Parses /proc/net/netstat, which is organized as pairs of lines: a "<Section>: <name> <name> ..." header line
// followed by a "<Section>: <value> <value> ..." values line with matching columns. Reads the TcpExt section and
// extracts the listen/SYN backlog drop and SYN cookie counters (ListenOverflows, ListenDrops, TCPReqQFullDrop,
// TCPReqQFullDoCookies, SyncookiesRecv) into the provided NetworkCounters.
bool ReadNetstatCounters(NetworkCounters& counters)
{
    counters.listenOverflows = 0;
    counters.listenDrops = 0;
    counters.synQueueFullDrops = 0;
    counters.synCookiesSent = 0;
    counters.syncookiesRecv = 0;

    LOG_DEBUG("[NetworkCounters] ReadNetstatCounters() reading {}", NetworkCounterConstants::ProcNetNetstatPath);

    std::unordered_map<std::string, uint64_t> tcpExtValues;
    if (!ParseSectionFromNetworkFile(NetworkCounterConstants::TcpExtSection,
                                     NetworkCounterConstants::ProcNetNetstatPath,
                                     tcpExtValues))
    {
        LOG_WARN("[NetworkCounters] ReadNetstatCounters() failed to parse {} section from {}",
                 NetworkCounterConstants::TcpExtSection,
                 NetworkCounterConstants::ProcNetNetstatPath);
        return false;
    }

    const auto getVal = [&](const char* key) -> uint64_t
    {
        const auto it = tcpExtValues.find(key);
        return it != tcpExtValues.end() ? it->second : 0;
    };

    counters.listenOverflows   = getVal("ListenOverflows");
    counters.listenDrops       = getVal("ListenDrops");
    counters.synQueueFullDrops = getVal("TCPReqQFullDrop");
    counters.synCookiesSent    = getVal("TCPReqQFullDoCookies");
    counters.syncookiesRecv    = getVal("SyncookiesRecv");

    LOG_DEBUG("[NetworkCounters] ReadNetstatCounters() TcpExt ListenOverflows={} ListenDrops={} TCPReqQFullDrop={} TCPReqQFullDoCookies={} SyncookiesRecv={}",
             counters.listenOverflows,
             counters.listenDrops,
             counters.synQueueFullDrops,
             counters.synCookiesSent,
             counters.syncookiesRecv);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------