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
#include "ServerArgs.h"
#include <boost/program_options.hpp>
#include <sstream>
//---------------------------------------------------------------------------------------------------------------------
namespace po = boost::program_options;
//---------------------------------------------------------------------------------------------------------------------
ParseServerArgsResult ParseServerArgs(int argc, char* argv[], ServerArgs& outArgs, std::string& outUsageMessage)
{
    po::options_description options("CacheServer options");
    options.add_options()
        ("help,h", "Print this help message")
        ("port", po::value<int>(), "Port for CacheServer to listen on (required)")
        ("cacheRoot", po::value<std::string>()->default_value("."), "Root folder for the cache")
        ("sizeLimitBytes", po::value<uint64_t>()->default_value(DefaultCacheSizeLimitBytes),
            "Cache size limit in bytes")
        ("ttlWindowSeconds", po::value<uint32_t>()->default_value(DefaultCacheTTLWindowSizeSeconds),
            "Cache expiry TTL window size in seconds");

    po::variables_map variablesMap;
    try
    {
        po::store(po::parse_command_line(argc, argv, options), variablesMap);
        po::notify(variablesMap);
    }
    catch (const po::error& ex)
    {
        std::ostringstream errorStream;
        errorStream << "Error parsing arguments: " << ex.what() << "\n" << options;
        outUsageMessage = errorStream.str();
        return ParseServerArgsResult::Error;
    }

    if (variablesMap.count("help"))
    {
        std::ostringstream helpStream;
        helpStream << options;
        outUsageMessage = helpStream.str();
        return ParseServerArgsResult::HelpRequested;
    }

    if (!variablesMap.count("port"))
    {
        std::ostringstream errorStream;
        errorStream << "Error parsing arguments: --port is required\n" << options;
        outUsageMessage = errorStream.str();
        return ParseServerArgsResult::Error;
    }

    int port = variablesMap["port"].as<int>();
    if (port < 1 || port > 65535)
    {
        std::ostringstream errorStream;
        errorStream << "Error parsing arguments: --port must be between 1 and 65535, got " << port << "\n" << options;
        outUsageMessage = errorStream.str();
        return ParseServerArgsResult::Error;
    }

    uint64_t sizeLimitBytes = variablesMap["sizeLimitBytes"].as<uint64_t>();
    if (sizeLimitBytes == 0)
    {
        std::ostringstream errorStream;
        errorStream << "Error parsing arguments: --sizeLimitBytes must be greater than 0\n" << options;
        outUsageMessage = errorStream.str();
        return ParseServerArgsResult::Error;
    }

    outArgs.port = port;
    outArgs.cacheRootPath = variablesMap["cacheRoot"].as<std::string>();
    outArgs.cacheSizeLimitBytes = sizeLimitBytes;
    outArgs.cacheTTLWindowSeconds = variablesMap["ttlWindowSeconds"].as<uint32_t>();
    return ParseServerArgsResult::Success;
}
//---------------------------------------------------------------------------------------------------------------------
