///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// This file is a container for varions types of clients - currently, PerfClient and EvictionClient.
// PerfClient: generates random data and uploads/downloads it to/from the cache server. It measures the throughput of the
// cache server.
// EvictionClient: requests the cache server to delete files based on last access time time.
// LastAccessTimeDistributionClient: requests the cache server to provide the distribution of last access times of cache.
//---------------------------------------------------------------------------------------------------------------------
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iostream>
#include <random>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <iomanip>
#include <ctime>

#include "CacheClientImpl.h"
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "LogUtils.h"
//---------------------------------------------------------------------------------------------------------------------
namespace po = boost::program_options;
//---------------------------------------------------------------------------------------------------------------------
constexpr const std::size_t CHUNK_SIZE = (32 * 1024 * 1024);
constexpr const std::size_t RETRY_COUNT_PER_CHUNK = 5;
constexpr const std::size_t UPLOAD_EXPIRY_DURATION_SECONDS = 3600; // 1 hour
//---------------------------------------------------------------------------------------------------------------------
std::atomic_uint_fast64_t nGlobalFailureCounter(0);
//---------------------------------------------------------------------------------------------------------------------
inline const void* GetRandomBuffer()
{
    static thread_local char* pBuffer = (char*)malloc(CHUNK_SIZE);
    for(uint64_t i = 0; i < std::min((uint64_t)50, CHUNK_SIZE); i++)
    {
        pBuffer[i] = rand() % 256;
    }
    return (const void*)pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
inline void* GetEmptyBuffer()
{
    static thread_local void* pBuffer = malloc(CHUNK_SIZE);
    return pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if upload succeeded else false
bool UploadWithRetries(
    /*[in]*/ std::string& strFileName,
    /*[in]*/ CacheClientImpl& client)
{
    for(std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        const void* buffer = GetRandomBuffer();
        CacheClientStatus eStatus =
            client.Upload(strFileName.c_str(), buffer, CHUNK_SIZE,
                          UPLOAD_EXPIRY_DURATION_SECONDS, nullptr, nullptr,
                          nullptr, true, nullptr);

        if(eStatus != CacheClientStatus::SUCCESS)
        {
            LOG_WARN("Upload {} failed with error {}", strFileName, eStatus);
            nGlobalFailureCounter++;
        }
        else
        {
            return true;
        }
    }

    LOG_CRITICAL("Upload {} failed after {} attempts", strFileName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if download succeeded else false
bool DownloadWithRetries(
    /*[in]*/ std::string& strFileName,
    /*[in]*/ CacheClientImpl& client)
{
    for(std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        void* buffer = GetEmptyBuffer();
        std::size_t nLength = CHUNK_SIZE;
        CacheClientStatus eStatus =
            client.Download(strFileName.c_str(), buffer, &nLength, nullptr, nullptr, 0, false, nullptr);

        if(eStatus != CacheClientStatus::SUCCESS)
        {
            LOG_WARN("Download {} failed with error {}", strFileName, eStatus);
            nGlobalFailureCounter++;
        }
        else
        {
            if(nLength != CHUNK_SIZE)
            {
                LOG_WARN("Expected file {} read length to be {} but got {}", strFileName, CHUNK_SIZE, nLength);
                return false;
            }

            return true;
        }
    }

    LOG_CRITICAL("Download {} failed after {} attempts", strFileName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
void Worker(/*[in]*/ bool bIsUpload,
            /*[in]*/ std::size_t nStartIndex,
            /*[in]*/ std::size_t nEndIndexExclusive,
            /*[in]*/ std::size_t nTotalChunksToTransfer,
            /*[in]*/ std::string strFilePrefix,
            /*[in]*/ CacheClientImpl& client,
            /*[out]*/ std::atomic_uint_fast64_t& nChunksSuccessfullyTransferred)
{
    LOG_INFO("Worker thread started. StartIndex: {}, EndIndexExclusive: {}", nStartIndex, nEndIndexExclusive);
    std::size_t totalChunksSucceeded = 0;
    for(std::size_t idx = nStartIndex; idx < nEndIndexExclusive; idx++)
    {
        std::string strFileName = strFilePrefix + std::to_string(idx);
        bool success = bIsUpload ? UploadWithRetries(strFileName, client) : DownloadWithRetries(strFileName, client);
        if(success)
        {
            nChunksSuccessfullyTransferred++;
        }

        if(idx % 100 == 0)
        {
            LOG_INFO("In progress. Total chunks successfully transferred: {}/{} ({} %)",
                     nChunksSuccessfullyTransferred,
                     nTotalChunksToTransfer,
                     (nChunksSuccessfullyTransferred * 100) / nTotalChunksToTransfer);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if all chunks were transferred successfully else false
bool RunPerfTest(/*[in]*/ int nParallelism,
                 /*[in]*/ uint64_t nDataSizeBytes,
                 /*[in]*/ CacheClientImpl& client,
                 /*[in]*/ std::string& strPrefix,
                 /*[in]*/ std::string& strMode)
{
    LOG_INFO("Current Test Mode is {}", strMode);

    std::size_t nTotalChunksToTransfer = nDataSizeBytes / CHUNK_SIZE;
    std::size_t nChunksPerThread = nTotalChunksToTransfer / nParallelism;
    if (nTotalChunksToTransfer % nParallelism != 0) {
        std::size_t nUpperBoundTotalChunksToTransfer = nTotalChunksToTransfer + nParallelism - (nTotalChunksToTransfer % nParallelism);
        nChunksPerThread = nUpperBoundTotalChunksToTransfer / nParallelism;
    }
    std::atomic_uint_fast64_t nChunksSuccessfullyTransferred(0);
    std::vector<std::thread*> vecThreads;
    bool bIsUpload = (strMode == "upload");
    auto startTime = std::chrono::high_resolution_clock::now();

    for(std::size_t nThreadIdx = 0; nThreadIdx < nParallelism; nThreadIdx++)
    {
        std::size_t nStartIndex = nThreadIdx * nChunksPerThread;
        // EndIndex is set to the chunk counter. If chunk counter surpasses totalChunkCount, then totalChunkCount is used.
        std::size_t nEndIndexExclusive = std::min((nThreadIdx + 1) * nChunksPerThread, nTotalChunksToTransfer);
        std::thread* pThread = new std::thread(Worker,
                                               bIsUpload,
                                               nStartIndex,
                                               nEndIndexExclusive,
                                               nTotalChunksToTransfer,
                                               strPrefix,
                                               std::ref(client),
                                               std::ref(nChunksSuccessfullyTransferred));
        vecThreads.push_back(pThread);
    }

    for(std::thread* pThread : vecThreads)
    {
        pThread->join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::size_t durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    durationMs = std::max(durationMs, (std::size_t)1);
    std::size_t nBytesTransferred = nChunksSuccessfullyTransferred * CHUNK_SIZE;
    std::size_t nThroughputMbps = (nBytesTransferred * 8) / (durationMs * 1000);

    LOG_INFO("=============================================================");
    LOG_INFO("Test complete. Details below.")
    LOG_INFO("Test Mode: {}", strMode);
    LOG_INFO("Throughput: {} Mbps {} Gbps", nThroughputMbps, nThroughputMbps/1024);
    LOG_INFO("Total bytes transferred: {}", nBytesTransferred);
    LOG_INFO("Total time taken: {} ms", durationMs);
    LOG_INFO("Expected chunks to tansfer: {}", nTotalChunksToTransfer);
    LOG_INFO("Actual chunks transferred succeeded: {}", nChunksSuccessfullyTransferred);
    LOG_INFO("Total individual failures: {}", nGlobalFailureCounter.load());
    LOG_INFO("=============================================================");

    // Single line summary for easy parsing
    LOG_INFO("Summary: mode {},start_timestamp {},end_timestamp {},duration_ms {},total_bytes_transferred {},"
             "throughput_Mbps {},total_individual_failures {},expected_chunks_to_transfer {},chunks_successfully_transferred {}",
             strMode,
             std::chrono::duration_cast<std::chrono::milliseconds>(startTime.time_since_epoch()).count(),
             std::chrono::duration_cast<std::chrono::milliseconds>(endTime.time_since_epoch()).count(),
             durationMs,
             nBytesTransferred,
             nThroughputMbps,
             nGlobalFailureCounter.load(),
             nTotalChunksToTransfer,
             nChunksSuccessfullyTransferred);

    uint64_t nTotalFailedChunks = nTotalChunksToTransfer - nChunksSuccessfullyTransferred;

    if (nTotalFailedChunks > 0)
    {
        LOG_ERROR("This iteration failed with {} chunks not transferred", nTotalFailedChunks);
        return false;
    }
    else
    {
        return true;
    }
}
//---------------------------------------------------------------------------------------------------------------------
po::variables_map parse_perf_args(/*[in]*/ int argc, /*[in]*/ char* argv[])
{
    try
    {
        po::options_description description("Perf test client options");
        description.add_options()("help", "produce help message")(
            "server-port", po::value<int>(), "Port to connect to the cache servers")(
            "server-hostname", po::value<std::string>(), "Hostname(s) of cache server, '#' separated")(
            "num-threads", po::value<int>(), "Number of threads to use for the perf test")(
            "data-size-GiB", po::value<int>(), "Total size of data to upload/download in GiB")(
            "prefix", po::value<std::string>(), "Prefix for file uploads. Must be same for upload and download")(
            "mode", po::value<std::string>(), "Mode ( 'upload' or 'download' )")(
            "num-iterations", po::value<int>()->default_value(1), "Number of times to run the perf test")(
            "ib-enabled", po::value<bool>()->default_value(false), "Enable Infiniband"
            );

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if(vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }

        if(!(vm.count("server-port") > 0 && vm.count("server-hostname") > 0 && vm.count("num-threads") > 0 &&
             vm.count("data-size-GiB") > 0 && vm.count("mode") > 0 && vm.count("prefix") > 0 &&
             vm.count("num-iterations") > 0 && vm.count("ib-enabled") > 0))
        {
            std::cout << description << std::endl;
            exit(1);
        }

        return vm;
    }
    catch(std::exception& e)
    {
        LOG_CRITICAL("Error: {}", e.what());
        exit(2);
    }
    catch(...)
    {
        LOG_CRITICAL("Exception of unknown type!");
        exit(3);
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Eviction args: only server-port server-hostname last-access-time(string)
po::variables_map parse_eviction_args(/*[in]*/ int argc, /*[in]*/ char* argv[])
{
    try
    {
        po::options_description description("Eviction client options");
        description.add_options()("help", "produce help message")(
            "server-port", po::value<int>(), "Port to connect to the cache servers")(
            "server-hostname", po::value<std::string>(), "Hostname(s) of cache server, '#' separated")(
            "last-access-time",
            po::value<std::string>(),
            "Last access time time as string in the ISO 8601 datetime format ending with Z (zero UTC offset): "
            "YYYY-MM-DDThh:mm:ss.msecZ Example: 2024-04-02T20:00:00.000Z");

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if (vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }

        if (!(vm.count("server-port") > 0 && vm.count("server-hostname") > 0 && vm.count("last-access-time") > 0))
        {
            std::cout << description << std::endl;
            exit(1);
        }

        return vm;
    }
    catch (std::exception& e)
    {
        LOG_CRITICAL("Error: {}", e.what());
        exit(2);
    }
    catch (...)
    {
        LOG_CRITICAL("Exception of unknown type!");
        exit(3);
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Last access time distribution args: only server-port server-hostname distribution-bucket-size-in-hours(int)
po::variables_map parse_last_access_dist_args(/*[in]*/ int argc, /*[in]*/ char* argv[])
{
    try
    {
        po::options_description description("Last access time distribution client options");
        description.add_options()("help", "produce help message")(
            "server-port", po::value<int>(), "Port to connect to the cache servers")(
            "server-hostname", po::value<std::string>(), "Hostname(s) of cache server, '#' separated")(
            "distribution-bucket-size-in-hours",
            po::value<int>(),
            "Size of the distribution bucket in hours");

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if (vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }

        if (!(vm.count("server-port") > 0 && vm.count("server-hostname") > 0 && vm.count("distribution-bucket-size-in-hours") > 0))
        {
            std::cout << description << std::endl;
            exit(1);
        }

        return vm;
    }
    catch (std::exception& e)
    {
        LOG_CRITICAL("Error: {}", e.what());
        exit(2);
    }
    catch (...)
    {
        LOG_CRITICAL("Exception of unknown type!");
        exit(3);
    }
}
//---------------------------------------------------------------------------------------------------------------------
int PerfClient(int argc, char *argv[])
{
    auto args = parse_perf_args(argc, argv);
    auto strHostnames = args["server-hostname"].as<std::string>();
    std::vector<std::string> vecHostnames;
    boost::split(vecHostnames, strHostnames, boost::is_any_of("#"));
    uint16_t nPort = args["server-port"].as<int>();
    std::size_t nNumThreads = args["num-threads"].as<int>();
    std::size_t nDataSizeGiB = args["data-size-GiB"].as<int>();
    std::string strMode = args["mode"].as<std::string>();
    std::string strPrefix = args["prefix"].as<std::string>();
    std::size_t nNumIterations = args["num-iterations"].as<int>();
    bool bIsIBEnabled = args["ib-enabled"].as<bool>();

    if(strMode != "upload" && strMode != "download")
    {
        LOG_CRITICAL("Invalid mode {}. Must be 'upload' or 'download'", strMode);
        exit(4);
    }

    std::vector<const char*> vecCstrHostnames;
    for(const std::string& hostname : vecHostnames)
    {
        vecCstrHostnames.push_back(hostname.c_str());
    }

    CacheClientImpl client;
    client.Initialize(vecCstrHostnames.data(),
                      vecCstrHostnames.size(),
                      nPort,            /*nCacheServerPort*/
                      bIsIBEnabled,     /*bEnableIB*/
                      false,             /*bEnableCalculateChecksum*/
                      false,            /*bEnableValidateChecksum*/
                      nNumThreads * 2,  /*nConnectionPoolSize*/
                      false,            /*bEnableAuth*/
                      nullptr,          /*strAccountName*/
                      nullptr,          /*strAccountKey*/
                      false,            /*bUseLocalServer*/
                      true,             /*bReuseConnections*/
                      CHUNK_SIZE);      /*nMaxBufferSizeBytes*/

    std::size_t nDataSizeBytes = nDataSizeGiB * 1024 * 1024 * 1024;
    bool bAllIterationsSucceeded = true;

    for (std::size_t i = 0; i < nNumIterations; i++)
    {
        if (!RunPerfTest(nNumThreads, nDataSizeBytes, client, strPrefix, strMode))
        {
            bAllIterationsSucceeded = false;
        }
    }

    LOG_INFO("All iterations succeeded: {}", bAllIterationsSucceeded);

    return bAllIterationsSucceeded ? 0 : 5;
}
//---------------------------------------------------------------------------------------------------------------------
int EvictionClient(int argc, char* argv[])
{
    auto args = parse_eviction_args(argc, argv);
    auto strHostnames = args["server-hostname"].as<std::string>();
    std::vector<std::string> vecHostnames;
    boost::split(vecHostnames, strHostnames, boost::is_any_of("#"));
    uint16_t nPort = args["server-port"].as<int>();
    std::string strLastAccessTime = args["last-access-time"].as<std::string>();
    std::vector<const char*> vecCstrHostnames;
    for (const std::string& hostname : vecHostnames)
    {
        vecCstrHostnames.push_back(hostname.c_str());
    }

    CacheClientImpl client;
    client.Initialize(vecCstrHostnames.data(),
                      vecCstrHostnames.size(),
                      nPort,                       /*nCacheServerPort*/
                      false,                       /*bEnableIB*/
                      false,                       /*bEnableCalculateChecksum*/
                      false,                       /*bEnableValidateChecksum*/
                      vecCstrHostnames.size() * 2, /*nConnectionPoolSize*/
                      false,                       /*bEnableAuth*/
                      nullptr,                     /*strAccountName*/
                      nullptr,                     /*strAccountKey*/
                      false,                       /*bUseLocalServer*/
                      true);                       /*bReuseConnections*/

    LOG_INFO("Requesting eviction from cache servers based on last access time time: {}",
             strLastAccessTime.c_str());

    CacheClientStatus status = client.DeleteByLastAccessTime(strLastAccessTime.c_str());

    if (status != CacheClientStatus::SUCCESS)
    {
        LOG_CRITICAL("Failed to request eviction, return code: {}", status);
        return static_cast<int>(status);
    }
    else
    {
        LOG_INFO("Successfully requested eviction");
        return 0;
    }
}
//---------------------------------------------------------------------------------------------------------------------
int LastAccessTimeDistributionClient(int argc, char* argv[])
{
    auto args = parse_last_access_dist_args(argc, argv);
    auto strHostnames = args["server-hostname"].as<std::string>();
    std::vector<std::string> vecHostnames;
    boost::split(vecHostnames, strHostnames, boost::is_any_of("#"));
    uint16_t nPort = args["server-port"].as<int>();
    uint64_t nDistributionBucketSizeInHours = args["distribution-bucket-size-in-hours"].as<int>();
    std::vector<const char*> vecCstrHostnames;
    for (const std::string& hostname : vecHostnames)
    {
        vecCstrHostnames.push_back(hostname.c_str());
    }

    CacheClientImpl client;
    client.Initialize(vecCstrHostnames.data(),
                      vecCstrHostnames.size(),
                      nPort,                       /*nCacheServerPort*/
                      false,                       /*bEnableIB*/
                      false,                       /*bEnableCalculateChecksum*/
                      false,                       /*bEnableValidateChecksum*/
                      vecCstrHostnames.size() * 2, /*nConnectionPoolSize*/
                      false,                       /*bEnableAuth*/
                      nullptr,                     /*strAccountName*/
                      nullptr,                     /*strAccountKey*/
                      false,                       /*bUseLocalServer*/
                      true);                       /*bReuseConnections*/

    
    LastAccessTimeDistribution *pLastAccessTimeDistribution = new LastAccessTimeDistribution();
    pLastAccessTimeDistribution->arrKeyValuePairs = nullptr;
    pLastAccessTimeDistribution->nKeyValuePairs = 0;
    LOG_INFO("Requesting get last access time distribution of cache chunks from cache servers");
    CacheClientStatus status = client.GetLastAccessTimeDistribution(nDistributionBucketSizeInHours, pLastAccessTimeDistribution);

    if (status != CacheClientStatus::SUCCESS)
    {
        LOG_CRITICAL("Failed to get last access distribution, return code: {}", status);
        return static_cast<int>(status);
    }
    else
    {
        LOG_INFO("Successfully requested get last access distribution");
        std::cout << "=== Output Start (CSV of bucket start timestamp,size in bytes, size in GiB) ===" << std::endl;
        for (int i = 0; i < pLastAccessTimeDistribution->nKeyValuePairs; i++)
        {
            std::tm* tm = std::gmtime(&pLastAccessTimeDistribution->arrKeyValuePairs[i].nKey);

            std::ostringstream oss;
            oss << std::put_time(tm, "%FT%T");
            oss << ".000Z";

            std::string strTimestamp = oss.str();
            uint64_t nSizeBytes = pLastAccessTimeDistribution->arrKeyValuePairs[i].nValue;
            uint64_t nSizeGiB = nSizeBytes / (1024 * 1024 * 1024);
            std::cout << strTimestamp << "," << nSizeBytes << " bytes," << nSizeGiB << " GiB" << std::endl;
        }
        std::cout << "=== Output End ===";
        return 0;
    }
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();

    if (argc < 2)
    {
        LOG_ERROR("Atleast one argument is required");
        return 1;
    }

    // Defaults to perf client, unless first argument specifies another client
    if (std::string(argv[1]) == std::string("--eviction-client"))
    {
        return EvictionClient(argc, argv);
    }
    else if (std::string(argv[1]) == std::string("--last-access-time-distribution-client"))
    {
        return LastAccessTimeDistributionClient(argc, argv);
    }
    else
    {
        return PerfClient(argc, argv);
    }

    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
