///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
// 
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

#include "StreamingClient.h"
#include "RemoteCacheClient.h"
#include "InMemoryAttributesCache.h"
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "LogUtils.h"
//---------------------------------------------------------------------------------------------------------------------
namespace po = boost::program_options;
//---------------------------------------------------------------------------------------------------------------------
constexpr const std::size_t DEFAULT_CHUNK_SIZE_IN_MiB = 32;
constexpr const std::size_t RETRY_COUNT_PER_CHUNK = 5;
std::string DEFAULT_TEST_BLOB_NAME = "/storageinterceptionpr1.blob.core.windows.net/dataset/tbs/500gb/test1GB.";
//---------------------------------------------------------------------------------------------------------------------
std::atomic_uint_fast64_t nGlobalFailureCounter(0);
//---------------------------------------------------------------------------------------------------------------------
inline void* GetEmptyBuffer(uint64_t nChunkSizeBytes)
{
    static thread_local void* pBuffer = malloc(nChunkSizeBytes);
    return pBuffer;
}

//---------------------------------------------------------------------------------------------------------------------
// Returns true if ReadFile succeeded else false
bool ReadFileWithRetries(
    /*[in]*/ std::string& strFileName,
    /*[in]*/ std::shared_ptr<StreamingClient>& pStreamingClient,
    /*[in]*/ uint64_t nChunkSizeBytes)
{
    for(std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        void* buffer = GetEmptyBuffer(nChunkSizeBytes);
        std::size_t nLength = nChunkSizeBytes;
        try
        {
            nLength = pStreamingClient->ReadFile(strFileName.c_str(), buffer, nLength, 0, uint64_t(20) * 1024 * 1024 * 1024, "" /* version */); // 20GB
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception occurred while reading file {}: {}", strFileName, e.what());
            return false;
        }
        catch (...)
        {
            LOG_ERROR("Unknown exception occurred while reading file {}", strFileName);
            return false;
        }

        if(nLength != nChunkSizeBytes)
        {
            LOG_WARN("Expected file {} read length to be {} but got {}", strFileName, nChunkSizeBytes, nLength);
            return false;
        }
        return true;
    }

    LOG_CRITICAL("ReadFile {} failed after {} attempts", strFileName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
void Worker(/*[in]*/ std::size_t nStartIndex,
            /*[in]*/ std::size_t nEndIndexExclusive,
            /*[in]*/ std::size_t nTotalChunksToTransfer,
            /*[in]*/ std::shared_ptr<StreamingClient>& pStreamingClient,
            /*[in]*/ uint64_t nChunkSizeBytes,
            /*[in]*/ const std::string& strRemoteBlobPath,
            /*[in]*/ bool bIsSingleBlob,
            /*[out]*/ std::atomic_uint_fast64_t& nChunksSuccessfullyTransferred)
{
    LOG_INFO("Worker thread started. StartIndex: {}, EndIndexExclusive: {}", nStartIndex, nEndIndexExclusive);
    std::size_t totalChunksSucceeded = 0;
    for(std::size_t idx = nStartIndex; idx < nEndIndexExclusive; idx++)
    {
        std::string strFileName;
        if(bIsSingleBlob)
        {
            strFileName = strRemoteBlobPath;
        }
        else
        {
            strFileName = strRemoteBlobPath + std::to_string(idx);
        }

        bool success = ReadFileWithRetries(strFileName, pStreamingClient, nChunkSizeBytes);
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
bool RunStorageInterceptPerfTest(/*[in]*/ int nParallelism,
                                 /*[in]*/ uint64_t nDataSizeBytes,
                                 /*[in]*/ std::shared_ptr<StreamingClient>& pStreamingClient,
                                 /*[in]*/ uint64_t nChunkSizeBytes,
                                 /*[in]*/ const std::string& strRemoteBlobPath,
                                 /*[in]*/ bool bIsSingleBlob)
{
    std::size_t nTotalChunksToTransfer = nDataSizeBytes / nChunkSizeBytes;
    std::size_t nChunksPerThread = nTotalChunksToTransfer / nParallelism;
    if (nTotalChunksToTransfer % nParallelism != 0) {
        std::size_t nUpperBoundTotalChunksToTransfer =
            nTotalChunksToTransfer + nParallelism - (nTotalChunksToTransfer % nParallelism);
        nChunksPerThread = nUpperBoundTotalChunksToTransfer / nParallelism;
    }
    std::atomic_uint_fast64_t nChunksSuccessfullyTransferred(0);
    std::vector<std::thread*> vecThreads;
    auto startTime = std::chrono::high_resolution_clock::now();

    for(std::size_t nThreadIdx = 0; nThreadIdx < nParallelism; nThreadIdx++)
    {
        std::size_t nStartIndex = nThreadIdx * nChunksPerThread;
        // EndIndex is set to the chunk counter. If chunk counter surpasses totalChunkCount, then totalChunkCount is used.
        std::size_t nEndIndexExclusive = std::min((nThreadIdx + 1) * nChunksPerThread, nTotalChunksToTransfer);
        std::thread* pThread = new std::thread(Worker,
                                               nStartIndex,
                                               nEndIndexExclusive,
                                               nTotalChunksToTransfer,
                                               std::ref(pStreamingClient),
                                               nChunkSizeBytes,
                                               strRemoteBlobPath,
                                               bIsSingleBlob,
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
    std::size_t nBytesTransferred = nChunksSuccessfullyTransferred * nChunkSizeBytes;
    std::size_t nThroughputMbps = (nBytesTransferred * 8) / (durationMs * 1000);

    LOG_INFO("=============================================================");
    LOG_INFO("Test complete. Details below.")
    LOG_INFO("Throughput: {} Mbps {} Gbps", nThroughputMbps, nThroughputMbps/1024);
    LOG_INFO("Total bytes transferred: {}", nBytesTransferred);
    LOG_INFO("Total time taken: {} ms", durationMs);
    LOG_INFO("Expected chunks to tansfer: {}", nTotalChunksToTransfer);
    LOG_INFO("Actual chunks transferred succeeded: {}", nChunksSuccessfullyTransferred);
    LOG_INFO("Total individual failures: {}", nGlobalFailureCounter.load());
    LOG_INFO("=============================================================");

    // Single line summary for easy parsing
    LOG_INFO("Summary: start_timestamp {},end_timestamp {},duration_ms {},total_bytes_transferred {},"
             "throughput_Mbps {},total_individual_failures {},expected_chunks_to_transfer {},chunks_successfully_transferred {}",
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
        po::options_description description("Perf test pStreamingClient options");
        description.add_options()("help", "produce help message")(
            "intercept-path", po::value<std::string>(), "Local Node path for interception")(
            "server-hostname", po::value<std::string>(), "Hostname(s) of cache server, '#' separated")(
            "server-port", po::value<int>(), "Port to connect to the cache servers")(
            "num-threads", po::value<int>(), "Number of threads to use for the perf test")(
            "data-size-GiB", po::value<int>(), "Total size of data to upload/download in GiB")(
            "num-iterations", po::value<int>()->default_value(1), "Number of times to run the perf test")(
            "ib-enabled", po::value<bool>()->default_value(false), "Enable Infiniband")(
            "chunk-size-MiB", po::value<int>()->default_value(DEFAULT_CHUNK_SIZE_IN_MiB), "Chunk size in MiB")(
            "single-blob", po::value<bool>()->default_value(false), "Read single blob")(
            "remote-blob-path", po::value<std::string>()->default_value(DEFAULT_TEST_BLOB_NAME),
                "Remote blob path to use for the test");

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if(vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }
        if(!(vm.count("intercept-path") > 0 && vm.count("server-port") > 0 && vm.count("server-hostname") > 0 &&
             vm.count("num-threads") > 0 && vm.count("data-size-GiB") > 0 && vm.count("num-iterations") > 0 &&
             vm.count("ib-enabled") > 0))
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

void GenerateStorageInterceptorConfigFile(
    /*[in]*/ const std::string& strInterceptPath,
    /*[in]*/ const std::string& strCacheServerList,
    /*[in]*/ uint16_t CacheServerPort,
    /*[in]*/ bool bIsIBEnabled,
    /*[in]*/ uint64_t nChunkSize)
{
    std::string config_path = "/tmp/storageIntercept.config";
    std::ofstream config_file(config_path);
    if (config_file.is_open()) {
        config_file << "storagePath " << strInterceptPath << "\n";
        config_file << "type blob\n";
        config_file << "azBlobDynamicAccount true\n";
        config_file << "azBlobSASToken ${STORAGE_INTCPT_SAS_TOKEN}\n";
        config_file << "enableLargePrefetch true\n";
        config_file << "prefetchWindowSizeInMB 32\n";
        config_file << "streamingChunkSize 33554432\n";
        config_file << "maxConnsPerBlobClient 320\n";
        config_file << "logRefreshIntervalInSeconds 300\n";
        config_file << "prefetchCachePerFileSizeInMB 102500\n";
        config_file << "prefetchCacheTotalSizeInMB 102500\n";
        if (strCacheServerList != "" && CacheServerPort != 0) {
            config_file << "cacheEnable true\n";
            config_file << "cacheEnableRemote true\n";
            config_file << "cacheIBEnabled " << (bIsIBEnabled ? "true" : "false") << "\n";
            config_file << "cacheServerList " << strCacheServerList << "\n";
            config_file << "cacheServerPort " << CacheServerPort << "\n";
            config_file << "cacheMaxUploadBufferSizePerProcessGB 200\n";
            config_file << "cacheNumUploadThreadsPerProcess 10\n";
            config_file << "attributesCacheEnable true\n";
            config_file << "attributesCacheUseLocalServer false\n";
            config_file << "attributesCacheUseRemoteServer true\n";
            LOG_DEBUG("Configured cache related configurations in {}", config_path);
        } else {
            LOG_WARN("CACHE_SERVER_LIST or CACHE_SERVER_PORT environment variable is not set, working in Remote Blob mode only.");
        }
    } else {
        LOG_ERROR("Failed to open config file: {}", config_path);
    }
    config_file.close();
    LOG_INFO("Storage Interceptor config file generated at: {}", config_path);
}

//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptorClient(int argc, char *argv[])
{
    auto args = parse_perf_args(argc, argv);
    auto strInterceptionPath = args["intercept-path"].as<std::string>();
    auto strHostnames = args["server-hostname"].as<std::string>();
    uint16_t nPort = args["server-port"].as<int>();
    std::size_t nNumThreads = args["num-threads"].as<int>();
    std::size_t nDataSizeGiB = args["data-size-GiB"].as<int>();
    std::size_t nNumIterations = args["num-iterations"].as<int>();
    bool bIsIBEnabled = args["ib-enabled"].as<bool>();
    bool bIsSingleBlob = args["single-blob"].as<bool>();
    std::size_t nChunkSizeMiB = args["chunk-size-MiB"].as<int>();
    auto strRemoteBlobPath = args["remote-blob-path"].as<std::string>();
    LOG_INFO("Storage Interceptor Client started with the following parameters:");
    LOG_INFO("Interception Path: {}", strInterceptionPath);
    LOG_INFO("Cache Server Hostnames: {}", strHostnames);
    LOG_INFO("Cache Server Port: {}", nPort);
    LOG_INFO("Number of Threads: {}", nNumThreads);
    LOG_INFO("Data Size (GiB): {}", nDataSizeGiB);
    LOG_INFO("Number of Iterations: {}", nNumIterations);
    LOG_INFO("Infiniband Enabled: {}", bIsIBEnabled);
    LOG_INFO("Single Blob: {}", bIsSingleBlob);
    LOG_INFO("Chunk Size (MiB): {}", nChunkSizeMiB);
    LOG_INFO("Remote Blob Path: {}", strRemoteBlobPath);

    GenerateStorageInterceptorConfigFile(strInterceptionPath, strHostnames, nPort, bIsIBEnabled, nChunkSizeMiB);

    std::shared_ptr<StreamingClient> m_spStreamingClient =
        std::make_shared<StreamingClient>(
            "", std::make_shared<RemoteCacheClient>(), std::make_shared<InMemoryAttributesCache>());

    std::size_t nDataSizeBytes = nDataSizeGiB * 1024 * 1024 * 1024;
    std::size_t nChunkSizeBytes = nChunkSizeMiB * 1024 * 1024;
    bool bAllIterationsSucceeded = true;
    for (std::size_t i = 0; i < nNumIterations; i++)
    {
        if (!RunStorageInterceptPerfTest(nNumThreads, nDataSizeBytes, m_spStreamingClient, nChunkSizeBytes, strRemoteBlobPath, bIsSingleBlob))
        {
            bAllIterationsSucceeded = false;
        }
    }

    LOG_INFO("All iterations succeeded: {}", bAllIterationsSucceeded);

    return bAllIterationsSucceeded ? 0 : 5;
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();
    LOG_INFO("Starting Up Storage Interceptor Client");

    if (argc < 2)
    {
        LOG_ERROR("Atleast one argument is required");
        return 1;
    }

    return StorageInterceptorClient(argc, argv);
}
//---------------------------------------------------------------------------------------------------------------------
