///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// This file is a container for varions types of clients - currently, PerfClient and EvictionClient.
// PerfClient: generates random data and uploads/downloads it to/from the cache server. It measures the throughput of the
// cache server.
// EvictionClient: requests the cache server to delete files based on last access time time.
// LastAccessTimeDistributionClient: requests the cache server to provide the distribution of last access times of cache.
//---------------------------------------------------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <azure/storage/blobs.hpp>
#include <azure/identity/default_azure_credential.hpp>
#include <azure/identity/managed_identity_credential.hpp>
#include <azure/core/diagnostics/logger.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
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
constexpr const std::size_t DEFAULT_CHUNK_SIZE_BYTES = 32 * 1024 * 1024; // 32 MiB default
constexpr const std::size_t RETRY_COUNT_PER_CHUNK = 5;
constexpr const std::size_t UPLOAD_EXPIRY_DURATION_SECONDS = 3600; // 1 hour
constexpr const char* LOCALHOST = "127.0.0.1";
//---------------------------------------------------------------------------------------------------------------------
std::atomic_uint_fast64_t nGlobalFailureCounter(0);
//---------------------------------------------------------------------------------------------------------------------
inline const void* GetRandomBuffer(std::size_t nChunkSizeBytes)
{
    static thread_local char* pBuffer = (char*)malloc(nChunkSizeBytes);
    for(uint64_t i = 0; i < std::min((uint64_t)50, nChunkSizeBytes); i++)
    {
        pBuffer[i] = rand() % 256;
    }
    return (const void*)pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
inline void* GetEmptyBuffer(std::size_t nChunkSizeBytes)
{
    static thread_local void* pBuffer = malloc(nChunkSizeBytes);
    return pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if upload succeeded else false
bool UploadWithRetries(
    /*[in]*/ std::string& strFileName,
    /*[in]*/ CacheClientImpl& client,
    /*[in]*/ std::size_t nChunkSizeBytes)
{
    for(std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        const void* buffer = GetRandomBuffer(nChunkSizeBytes);
        CacheClientStatus eStatus =
            client.Upload(strFileName.c_str(), buffer, nChunkSizeBytes,
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
    /*[in]*/ CacheClientImpl& client,
    /*[in]*/ std::size_t nChunkSizeBytes)
{
    for(std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        void* buffer = GetEmptyBuffer(nChunkSizeBytes);
        std::size_t nLength = nChunkSizeBytes;
        CacheClientStatus eStatus =
            client.Download(strFileName.c_str(), buffer, &nLength, nullptr, nullptr, 0, false, nullptr);

        if(eStatus != CacheClientStatus::SUCCESS)
        {
            LOG_WARN("Download {} failed with error {}", strFileName, eStatus);
            nGlobalFailureCounter++;
        }
        else
        {
            if(nLength != nChunkSizeBytes)
            {
                LOG_WARN("Expected file {} read length to be {} but got {}", strFileName, nChunkSizeBytes, nLength);
                return false;
            }

            return true;
        }
    }

    LOG_CRITICAL("Download {} failed after {} attempts", strFileName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<int64_t> Worker(/*[in]*/ bool bIsUpload,
            /*[in]*/ std::size_t nStartIndex,
            /*[in]*/ std::size_t nEndIndexExclusive,
            /*[in]*/ std::size_t nTotalChunksToTransfer,
            /*[in]*/ const std::string& strFilePrefix,
            /*[in]*/ CacheClientImpl& client,
            /*[in]*/ std::size_t nChunkSizeBytes,
            /*[out]*/ std::atomic_uint_fast64_t& nChunksSuccessfullyTransferred)
{
    LOG_INFO("Cache worker thread started. StartIndex: {}, EndIndexExclusive: {}", nStartIndex, nEndIndexExclusive);
    std::vector<int64_t> perChunkMs;
    if (nEndIndexExclusive > nStartIndex)
    {
        perChunkMs.reserve(nEndIndexExclusive - nStartIndex);
    }
    
    for(std::size_t idx = nStartIndex; idx < nEndIndexExclusive; idx++)
    {
        std::string strFileName = strFilePrefix + std::to_string(idx);
        auto transferStart = std::chrono::high_resolution_clock::now();
        bool success = bIsUpload ? UploadWithRetries(strFileName, client, nChunkSizeBytes) : DownloadWithRetries(strFileName, client, nChunkSizeBytes);
        auto transferEnd = std::chrono::high_resolution_clock::now();
        
        if(success)
        {
            nChunksSuccessfullyTransferred++;
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(transferEnd - transferStart).count();
            perChunkMs.push_back(durationMs);
        }

        if(idx % 100 == 0)
        {
            LOG_INFO("In progress. Total chunks successfully transferred: {}/{} ({} %)",
                     nChunksSuccessfullyTransferred,
                     nTotalChunksToTransfer,
                     (nChunksSuccessfullyTransferred * 100) / nTotalChunksToTransfer);
        }
    }
    
    return perChunkMs;
}

int64_t GetPercentile(const std::vector<int64_t>& sortedData, double percentile)
{
    if (sortedData.empty())
    {
        return 0;
    }
    auto clamped = std::max(0.0, std::min(100.0, percentile));
    auto index = static_cast<std::size_t>((clamped / 100.0) * (sortedData.size() - 1));
    index = std::min(index, sortedData.size() - 1);
    return sortedData[index];
}
//---------------------------------------------------------------------------------------------------------------------
// Template function to run perf tests with any client type and worker function.
// WorkerFunc signature: std::vector<int64_t>(bool bIsUpload, std::size_t nStartIndex, std::size_t nEndIndexExclusive,
//                                            std::size_t nTotalChunksToTransfer, const std::string& strPrefix,
//                                            TClient& client, std::size_t nChunkSizeBytes,
//                                            std::atomic_uint_fast64_t& nChunksSuccessfullyTransferred)
template <typename TClient, typename WorkerFunc>
bool RunPerfTestImpl(/*[in]*/ int nParallelism,
                     /*[in]*/ uint64_t nDataSizeBytes,
                     /*[in]*/ TClient& client,
                     /*[in]*/ const std::string& strPrefix,
                     /*[in]*/ const std::string& strMode,
                     /*[in]*/ const std::string& strTestName,
                     /*[in]*/ std::size_t nChunkSizeBytes,
                     /*[in]*/ WorkerFunc workerFunc)
{
    LOG_INFO("Current Test Mode is {}{}", strMode, strTestName.empty() ? "" : " (" + strTestName + ")");

    if (nChunkSizeBytes > nDataSizeBytes) {
        LOG_WARN("Chunk size {} bytes is greater than total data size {} bytes. Clamping chunk size to data size.",
                 nChunkSizeBytes, nDataSizeBytes);
        nChunkSizeBytes = nDataSizeBytes;
    }

    std::size_t nTotalChunksToTransfer = nDataSizeBytes / nChunkSizeBytes;

    std::size_t nChunksPerThread = nTotalChunksToTransfer / nParallelism;
    if (nTotalChunksToTransfer % nParallelism != 0)
    {
        std::size_t nUpperBoundTotalChunksToTransfer = nTotalChunksToTransfer + nParallelism - (nTotalChunksToTransfer % nParallelism);
        nChunksPerThread = nUpperBoundTotalChunksToTransfer / nParallelism;
    }
    std::atomic_uint_fast64_t nChunksSuccessfullyTransferred(0);
    std::vector<std::future<std::vector<int64_t>>> vecFutures;
    bool bIsUpload = (strMode == "upload");
    auto startTime = std::chrono::high_resolution_clock::now();

    for (std::size_t nThreadIdx = 0; nThreadIdx < nParallelism; nThreadIdx++)
    {
        std::size_t nStartIndex = nThreadIdx * nChunksPerThread;
        std::size_t nEndIndexExclusive = std::min((nThreadIdx + 1) * nChunksPerThread, nTotalChunksToTransfer);
        vecFutures.emplace_back(std::async(std::launch::async,
                                workerFunc,
                                bIsUpload,
                                nStartIndex,
                                nEndIndexExclusive,
                                nTotalChunksToTransfer,
                                std::cref(strPrefix),
                                std::ref(client),
                                nChunkSizeBytes,
                                std::ref(nChunksSuccessfullyTransferred)));
    }

    // Wait for all futures to complete (without extracting results yet)
    for (auto& future : vecFutures)
    {
        future.wait();
    }

    auto endTime = std::chrono::high_resolution_clock::now();

    // Collect per-chunk times from all futures
    std::vector<int64_t> allPerChunkMs;
    for (auto& future : vecFutures)
    {
        auto workerPerChunkMs = future.get();
        allPerChunkMs.insert(allPerChunkMs.end(), workerPerChunkMs.begin(), workerPerChunkMs.end());
    }

    std::size_t durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    durationMs = std::max(durationMs, (std::size_t)1);
    std::size_t nBytesTransferred = nChunksSuccessfullyTransferred * nChunkSizeBytes;
    std::size_t nThroughputMbps = (nBytesTransferred * 8) / (durationMs * 1000);
    // GiBps = (bytes / ms) * (1000 ms/s) / (1024^3 bytes/GiB)
    //       = bytes * 1000 / (ms * 1024 * 1024 * 1024)
    double nThroughputGiBps = static_cast<double>(nBytesTransferred) * 1000.0 / (static_cast<double>(durationMs) * 1024.0 * 1024.0 * 1024.0);

    std::sort(allPerChunkMs.begin(), allPerChunkMs.end());
    auto perChunkP50Ms = GetPercentile(allPerChunkMs, 50.0);
    auto perChunkP90Ms = GetPercentile(allPerChunkMs, 90.0);
    auto perChunkP99Ms = GetPercentile(allPerChunkMs, 99.0);

    std::string strLogTestName = strTestName.empty() ? "Test" : strTestName + " Test";
    LOG_INFO("=============================================================");
    LOG_INFO("{} complete. Details below.", strLogTestName);
    LOG_INFO("Test Mode: {}", strMode);
    LOG_INFO("Throughput: {} Mbps {} GiBps", nThroughputMbps, nThroughputGiBps);
    LOG_INFO("Chunk size: {} bytes", nChunkSizeBytes);
    LOG_INFO("Per-chunk p50: {} ms, p90: {} ms, p99: {} ms", perChunkP50Ms, perChunkP90Ms, perChunkP99Ms);
    LOG_INFO("Total bytes transferred: {}", nBytesTransferred);
    LOG_INFO("Total time taken: {} ms", durationMs);
    LOG_INFO("Expected chunks to transfer: {}", nTotalChunksToTransfer);
    LOG_INFO("Actual chunks transferred succeeded: {}", nChunksSuccessfullyTransferred);
    LOG_INFO("Total individual failures: {}", nGlobalFailureCounter.load());
    LOG_INFO("=============================================================");

    // Single line summary for easy parsing
    LOG_INFO("Summary: mode {},chunksize_bytes {},start_timestamp {},end_timestamp {},duration_ms {},total_bytes_transferred {},"
             "throughput_Mbps {},total_individual_failures {},expected_chunks_to_transfer {},chunks_successfully_transferred {},"
             "perchunk_p50_ms {},perchunk_p90_ms {},perchunk_p99_ms {}",
             strMode,
             nChunkSizeBytes,
             std::chrono::duration_cast<std::chrono::milliseconds>(startTime.time_since_epoch()).count(),
             std::chrono::duration_cast<std::chrono::milliseconds>(endTime.time_since_epoch()).count(),
             durationMs,
             nBytesTransferred,
             nThroughputMbps,
             nGlobalFailureCounter.load(),
             nTotalChunksToTransfer,
             nChunksSuccessfullyTransferred,
             perChunkP50Ms,
             perChunkP90Ms,
             perChunkP99Ms);

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
// Returns true if all chunks were transferred successfully else false
bool RunPerfTest(/*[in]*/ int nParallelism,
                 /*[in]*/ uint64_t nDataSizeBytes,
                 /*[in]*/ CacheClientImpl& client,
                 /*[in]*/ const std::string& strPrefix,
                 /*[in]*/ const std::string& strMode,
                 /*[in]*/ std::size_t nChunkSizeBytes)
{
    return RunPerfTestImpl(nParallelism, nDataSizeBytes, client, strPrefix, strMode, "", nChunkSizeBytes, Worker);
}

//---------------------------------------------------------------------------------------------------------------------
// Returns true if blob upload succeeded else false
bool BlobUploadWithRetries(
    /*[in]*/ const std::string& strBlobName,
    /*[in]*/ Azure::Storage::Blobs::BlobContainerClient& containerClient,
    /*[in]*/ std::size_t nChunkSizeBytes)
{
    for (std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        try
        {
            const void* buffer = GetRandomBuffer(nChunkSizeBytes);
            auto blobClient = containerClient.GetBlockBlobClient(strBlobName);
            blobClient.UploadFrom(reinterpret_cast<const uint8_t*>(buffer), nChunkSizeBytes);
            return true;
        }
        catch (const Azure::Core::RequestFailedException& e)
        {
            LOG_WARN("Blob upload {} failed with error {} ({})", strBlobName, e.ErrorCode, e.what());
            nGlobalFailureCounter++;
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Blob upload {} failed with error {}", strBlobName, e.what());
            nGlobalFailureCounter++;
        }
    }

    LOG_CRITICAL("Blob upload {} failed after {} attempts", strBlobName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if blob download succeeded else false
bool BlobDownloadWithRetries(
    /*[in]*/ const std::string& strBlobName,
    /*[in]*/ Azure::Storage::Blobs::BlobContainerClient& containerClient,
    /*[in]*/ std::size_t nChunkSizeBytes)
{
    for (std::size_t attempt = 0; attempt < RETRY_COUNT_PER_CHUNK; attempt++)
    {
        try
        {
            void* buffer = GetEmptyBuffer(nChunkSizeBytes);
            auto blobClient = containerClient.GetBlockBlobClient(strBlobName);
            auto downloadResult = blobClient.DownloadTo(reinterpret_cast<uint8_t*>(buffer), nChunkSizeBytes);
            
            if (!downloadResult.Value.ContentRange.Length.HasValue())
            {
                LOG_WARN("Blob {} download response missing ContentRange.Length", strBlobName);
                return false;
            }

            if (downloadResult.Value.ContentRange.Length.Value() != nChunkSizeBytes)
            {
                LOG_WARN("Expected blob {} read length to be {} but got {}", 
                         strBlobName, nChunkSizeBytes, downloadResult.Value.ContentRange.Length.Value());
                return false;
            }
            return true;
        }
        catch (const Azure::Core::RequestFailedException& e)
        {
            LOG_WARN("Blob download {} failed with error {} ({})", strBlobName, e.ErrorCode, e.what());
            nGlobalFailureCounter++;
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Blob download {} failed with error {}", strBlobName, e.what());
            nGlobalFailureCounter++;
        }
    }

    LOG_CRITICAL("Blob download {} failed after {} attempts", strBlobName, RETRY_COUNT_PER_CHUNK);
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<int64_t> BlobWorker(/*[in]*/ bool bIsUpload,
                /*[in]*/ std::size_t nStartIndex,
                /*[in]*/ std::size_t nEndIndexExclusive,
                /*[in]*/ std::size_t nTotalChunksToTransfer,
                /*[in]*/ const std::string& strBlobPrefix,
                /*[in]*/ Azure::Storage::Blobs::BlobContainerClient& containerClient,
                /*[in]*/ std::size_t nChunkSizeBytes,
                /*[out]*/ std::atomic_uint_fast64_t& nChunksSuccessfullyTransferred)
{
    LOG_INFO("Blob worker thread started. StartIndex: {}, EndIndexExclusive: {}", nStartIndex, nEndIndexExclusive);
    std::vector<int64_t> perChunkMs;
    perChunkMs.reserve(nEndIndexExclusive - nStartIndex);

    for (std::size_t idx = nStartIndex; idx < nEndIndexExclusive; idx++)
    {
        std::string strBlobName = strBlobPrefix + std::to_string(idx);
        auto transferStart = std::chrono::high_resolution_clock::now();
        bool success = bIsUpload ? BlobUploadWithRetries(strBlobName, containerClient, nChunkSizeBytes)
                                 : BlobDownloadWithRetries(strBlobName, containerClient, nChunkSizeBytes);
        auto transferEnd = std::chrono::high_resolution_clock::now();

        if (success)
        {
            nChunksSuccessfullyTransferred++;
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(transferEnd - transferStart).count();
            perChunkMs.push_back(durationMs);
        }

        if (idx % 100 == 0)
        {
            LOG_INFO("In progress. Total chunks successfully transferred: {}/{} ({} %)",
                     nChunksSuccessfullyTransferred,
                     nTotalChunksToTransfer,
                     (nChunksSuccessfullyTransferred * 100) / nTotalChunksToTransfer);
        }
    }

    return perChunkMs;
}
//---------------------------------------------------------------------------------------------------------------------
// Returns true if all chunks were transferred successfully else false
bool RunPerfBlobDirectTest(int nParallelism,
    uint64_t nDataSizeBytes,
    Azure::Storage::Blobs::BlobContainerClient& containerClient,
    const std::string& strPrefix,
    const std::string& strMode,
    std::size_t nChunkSizeBytes)
{
    return RunPerfTestImpl(nParallelism, nDataSizeBytes, containerClient, strPrefix, strMode, "Direct Blob", nChunkSizeBytes, BlobWorker);
}


//---------------------------------------------------------------------------------------------------------------------
// Common options shared between perf test clients
void add_common_perf_options(po::options_description& description)
{
    description.add_options()
        ("help", "produce help message")
        ("num-threads", po::value<int>(), "Number of threads to use for the perf test")
        ("data-size-GiB", po::value<int>(), "Total size of data to upload/download in GiB")
        ("mode", po::value<std::string>(), "Mode ( 'upload' or 'download' )")
        ("prefix", po::value<std::string>(), "Prefix for file uploads. Must be same for upload and download")
        ("num-iterations", po::value<int>()->default_value(1), "Number of times to run the perf test")
        ("cache-retry-max-attempts", po::value<uint32_t>()->default_value(1), "Max attempts for cache-client internal retries (1 disables retries)")
        ("cache-retry-base-delay-ms", po::value<uint64_t>()->default_value(0), "Base delay (ms) for cache-client internal retries")
        ("cache-retry-max-delay-ms", po::value<uint64_t>()->default_value(0), "Max delay (ms) for cache-client internal retries")
        ("chunk-size-bytes", po::value<std::size_t>()->default_value(DEFAULT_CHUNK_SIZE_BYTES), "Size of each chunk in bytes (default: 33554432 = 32 MiB)");
}
//---------------------------------------------------------------------------------------------------------------------
// Validates common required perf test options are present
bool has_common_perf_options(const po::variables_map& vm)
{
    return vm.count("num-threads") > 0 && vm.count("data-size-GiB") > 0 &&
           vm.count("mode") > 0 && vm.count("prefix") > 0;
}
//---------------------------------------------------------------------------------------------------------------------
// Validates mode is either 'upload' or 'download'
bool is_valid_mode(const std::string& strMode)
{
    return strMode == "upload" || strMode == "download";
}
//---------------------------------------------------------------------------------------------------------------------
po::variables_map parse_perf_args(/*[in]*/ int argc, /*[in]*/ char* argv[])
{
    try
    {
        po::options_description description("Perf test client options");
        add_common_perf_options(description);
        description.add_options()
            ("server-port", po::value<int>(), "Port to connect to the cache servers")
            ("server-hostname", po::value<std::string>(), "Hostname(s) of cache server, '#' separated (optional when discovery is enabled)")
            ("ib-enabled", po::value<bool>()->default_value(false), "Enable Infiniband")
            ("is-discovery-enabled", po::value<bool>()->default_value(false), "Enable discovery endpoint (default: false)")
            ("discovery-endpoint", po::value<std::string>()->default_value("cacheserver-discovery"), "Discovery endpoint hostname (default: cacheserver-discovery)")
            ("enable-calculate-checksum", po::value<bool>()->default_value(false), "Enable checksum calculation on upload")
            ("enable-validate-checksum", po::value<bool>()->default_value(false), "Enable checksum validation on download")
            ("checksum-algorithm", po::value<std::string>()->default_value("xxhash32"), "Checksum algorithm to use ('xxhash32' or 'crc32')")
            ("connection-pool-size", po::value<uint32_t>(), "Connection pool size (default: num-threads * num-servers; required when discovery is enabled)");
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if(vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }

        bool bHasHostname = vm.count("server-hostname") > 0;
        bool bDiscoveryEnabled = vm["is-discovery-enabled"].as<bool>();

        if(!has_common_perf_options(vm) || vm.count("server-port") == 0)
        {
            std::cout << description << std::endl;
            exit(1);
        }

        // server-hostname is required when discovery is disabled
        if(!bDiscoveryEnabled && !bHasHostname)
        {
            LOG_CRITICAL("--server-hostname is required when discovery is disabled");
            std::cout << description << std::endl;
            exit(1);
        }

        // connection-pool-size is required when discovery is enabled (server count unknown at startup)
        if(bDiscoveryEnabled && vm.count("connection-pool-size") == 0)
        {
            LOG_CRITICAL("--connection-pool-size is required when discovery is enabled "
                         "(server count is not known at startup)");
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
// Direct blob client args for testing directly against Azure Blob Storage
po::variables_map parse_direct_blob_client_args(/*[in]*/ int argc, /*[in]*/ char* argv[])
{
    try
    {
        po::options_description description("Direct blob client options");
        add_common_perf_options(description);
        description.add_options()
            ("storage-account", po::value<std::string>(), "Azure storage account name")
            ("container", po::value<std::string>(), "Azure storage container name");

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(description).allow_unregistered().run(), vm);
        po::notify(vm);

        if (vm.count("help") > 0)
        {
            std::cout << description << std::endl;
            exit(0);
        }

        if (!(has_common_perf_options(vm) && vm.count("storage-account") > 0 && vm.count("container") > 0))
        {
            std::cout << description << std::endl;
            exit(1);
        }

        return vm;
    }
    catch (const std::exception& e)
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

    std::vector<std::string> vecHostnames;
    if (args.count("server-hostname") > 0)
    {
        auto strHostnames = args["server-hostname"].as<std::string>();
        boost::split(vecHostnames, strHostnames, boost::is_any_of("#"));
    }

    uint16_t nPort = args["server-port"].as<int>();
    std::size_t nNumThreads = args["num-threads"].as<int>();
    std::size_t nDataSizeGiB = args["data-size-GiB"].as<int>();
    std::string strMode = args["mode"].as<std::string>();
    std::string strPrefix = args["prefix"].as<std::string>();
    std::size_t nNumIterations = args["num-iterations"].as<int>();
    bool bIsIBEnabled = args["ib-enabled"].as<bool>();
    bool bIsDiscoveryEnabled = args["is-discovery-enabled"].as<bool>();
    std::string strDiscoveryEndpoint = args["discovery-endpoint"].as<std::string>();
    bool bEnableCalculateChecksum = args["enable-calculate-checksum"].as<bool>();
    bool bEnableValidateChecksum = args["enable-validate-checksum"].as<bool>();
    std::string strChecksumAlgo = args["checksum-algorithm"].as<std::string>();

    uint32_t nRetryMaxAttempts = args["cache-retry-max-attempts"].as<uint32_t>();
    uint64_t nRetryBaseDelayMs = args["cache-retry-base-delay-ms"].as<uint64_t>();
    uint64_t nRetryMaxDelayMs = args["cache-retry-max-delay-ms"].as<uint64_t>();
    std::size_t nChunkSizeBytes = args["chunk-size-bytes"].as<std::size_t>();
    LOG_INFO("Chunk size set to {} bytes", nChunkSizeBytes);

    if (bIsDiscoveryEnabled)
    {
        LOG_INFO("Discovery enabled, endpoint: {}", strDiscoveryEndpoint);
    }
    else
    {
        LOG_INFO("Server hostnames provided: {}", fmt::join(vecHostnames, ", "));
    }

    if (!is_valid_mode(strMode))
    {
        LOG_CRITICAL("Invalid mode {}. Must be 'upload' or 'download'", strMode);
        return 4;
    }

    std::vector<const char*> vecCstrHostnames;
    for (const std::string& hostname : vecHostnames)
    {
        vecCstrHostnames.push_back(hostname.c_str());
    }

    CacheClientImpl client;
    const CacheClientRetryConfig retryConfig{nRetryMaxAttempts, nRetryBaseDelayMs, nRetryMaxDelayMs};
    // When hostnames provided, size pool for all server*thread combos; otherwise let CacheClient use its default
    uint32_t nConnectionPoolSize = vecCstrHostnames.empty() ? 0 : nNumThreads * vecCstrHostnames.size();
    if (args.count("connection-pool-size") > 0)
    {
        nConnectionPoolSize = args["connection-pool-size"].as<uint32_t>();
        LOG_INFO("Using user-specified connection pool size: {}", nConnectionPoolSize);
    }
    if (nConnectionPoolSize == 0)
    {
        LOG_CRITICAL("Connection pool size must be greater than 0");
        return 1;
    }
    CacheClientStatus initStatus = client.Initialize(vecCstrHostnames.empty() ? nullptr : vecCstrHostnames.data(),
                                                     vecCstrHostnames.size(),
                                                     nPort,                        /*nCacheServerPort*/
                                                     bIsIBEnabled,                 /*bEnableIB*/
                                                     bEnableCalculateChecksum,     /*bEnableCalculateChecksum*/
                                                     bEnableValidateChecksum,      /*bEnableValidateChecksum*/
                                                     strChecksumAlgo.c_str(),      /*strChecksumAlgorithm*/
                                                     nConnectionPoolSize,           /*nConnectionPoolSize*/
                                                     false,                        /*bEnableAuth*/
                                                     nullptr,                      /*strAccountName*/
                                                     nullptr,                      /*strAccountKey*/
                                                     false,                        /*bUseLocalServer*/
                                                     true,                         /*bReuseConnections*/
                                                     bIsDiscoveryEnabled,          /*bIsCacheServerDiscoveryEnabled*/
                                                     5,                            /*nCacheServerDiscoveryIntervalInSeconds*/
                                                     strDiscoveryEndpoint.c_str(), /*nCacheServerDiscoveryEndpoint*/
                                                     false,                        /*bEnableConsistentHashing*/
                                                     retryConfig,
                                                     nChunkSizeBytes);              /*nMaxBufferSizeBytes*/

    if (initStatus != CacheClientStatus::SUCCESS)
    {
        LOG_CRITICAL("Failed to initialize cache client with error {}", initStatus);
        return static_cast<int>(initStatus);
    }

    std::size_t nDataSizeBytes = nDataSizeGiB * 1024 * 1024 * 1024;
    bool bAllIterationsSucceeded = true;

    for (std::size_t i = 0; i < nNumIterations; i++)
    {
        if (!RunPerfTest(nNumThreads, nDataSizeBytes, client, strPrefix, strMode, nChunkSizeBytes))
        {
            bAllIterationsSucceeded = false;
        }
    }

    LOG_INFO("All iterations succeeded: {}", bAllIterationsSucceeded);

    return bAllIterationsSucceeded ? 0 : 5;
}

int PerfDirectBlobClient(int argc, char *argv[])
{
    auto args = parse_direct_blob_client_args(argc, argv);
    std::size_t nNumThreads = args["num-threads"].as<int>();
    std::size_t nDataSizeGiB = args["data-size-GiB"].as<int>();
    std::string strMode = args["mode"].as<std::string>();
    std::string strPrefix = args["prefix"].as<std::string>();
    std::size_t nNumIterations = args["num-iterations"].as<int>();
    auto storageAccountName = args["storage-account"].as<std::string>();
    auto storageContainer = args["container"].as<std::string>();

    if (!is_valid_mode(strMode))
    {
        LOG_CRITICAL("Invalid mode {}. Must be 'upload' or 'download'", strMode);
        return 4;
    }

    // Enable Azure SDK diagnostic logging
    Azure::Core::Diagnostics::Logger::SetLevel(Azure::Core::Diagnostics::Logger::Level::Warning);
    Azure::Core::Diagnostics::Logger::SetListener([](Azure::Core::Diagnostics::Logger::Level level, const std::string& message) {
        switch (level) {
            case Azure::Core::Diagnostics::Logger::Level::Error:
                LOG_ERROR("[AzureSDK] {}", message);
                break;
            case Azure::Core::Diagnostics::Logger::Level::Warning:
                LOG_WARN("[AzureSDK] {}", message);
                break;
            default:
                LOG_INFO("[AzureSDK] {}", message);
                break;
        }
    });

    // Log workload identity environment variables for debugging
    const char* azureClientId = std::getenv("AZURE_CLIENT_ID");
    const char* azureTenantId = std::getenv("AZURE_TENANT_ID");
    const char* azureFederatedTokenFile = std::getenv("AZURE_FEDERATED_TOKEN_FILE");
    const char* azureAuthorityHost = std::getenv("AZURE_AUTHORITY_HOST");
    
    LOG_INFO("Workload Identity environment variables:");
    LOG_INFO("  AZURE_CLIENT_ID: {}", azureClientId ? azureClientId : "(not set)");
    LOG_INFO("  AZURE_TENANT_ID: {}", azureTenantId ? azureTenantId : "(not set)");
    LOG_INFO("  AZURE_FEDERATED_TOKEN_FILE: {}", azureFederatedTokenFile ? azureFederatedTokenFile : "(not set)");
    LOG_INFO("  AZURE_AUTHORITY_HOST: {}", azureAuthorityHost ? azureAuthorityHost : "(not set)");

    auto defaultAzureCredential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
    auto accountURL = "https://" + storageAccountName + ".blob.core.windows.net";
    Azure::Storage::Blobs::BlobServiceClient blobServiceClient{accountURL, defaultAzureCredential};

    auto containerClient = blobServiceClient.GetBlobContainerClient(storageContainer);

    // Preflight check: verify container exists and is accessible
    // This provides a clear error message instead of cryptic failures during upload/download
    // Retry with backoff since role assignments may take time to propagate
    LOG_INFO("Verifying container exists and is accessible...");
    constexpr int maxRetries = 5;
    constexpr int initialBackoffSeconds = 5;
    bool containerAccessVerified = false;
    
    for (int attempt = 1; attempt <= maxRetries && !containerAccessVerified; ++attempt)
    {
        try
        {
            containerClient.CreateIfNotExists();
            LOG_INFO("Container verification successful");
            containerAccessVerified = true;
        }
        catch (const Azure::Core::RequestFailedException& e)
        {
            if (attempt < maxRetries)
            {
                int backoffSeconds = initialBackoffSeconds * attempt;
                LOG_WARN("Container access check failed (attempt {}/{}): {} ({}). "
                         "Retrying in {} seconds (role assignment may still be propagating)...",
                         attempt, maxRetries, e.ErrorCode, e.what(), backoffSeconds);
                std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
            }
            else
            {
                LOG_CRITICAL("Failed to access container '{}' in storage account '{}' after {} attempts: {} ({})",
                             storageContainer, storageAccountName, maxRetries, e.ErrorCode, e.what());
                LOG_CRITICAL("Please verify: 1) Storage account name is correct, 2) Container exists, "
                             "3) Identity has 'Storage Blob Data Contributor' role on the container");
                return 6;
            }
        }
        catch (const std::exception& e)
        {
            if (attempt < maxRetries)
            {
                int backoffSeconds = initialBackoffSeconds * attempt;
                LOG_WARN("Container access check failed (attempt {}/{}): {}. Retrying in {} seconds...",
                         attempt, maxRetries, e.what(), backoffSeconds);
                std::this_thread::sleep_for(std::chrono::seconds(backoffSeconds));
            }
            else
            {
                LOG_CRITICAL("Failed to verify container access after {} attempts: {}", maxRetries, e.what());
                return 6;
            }
        }
    }

    std::size_t nChunkSizeBytes = args["chunk-size-bytes"].as<std::size_t>();
    LOG_INFO("Chunk size set to {} bytes", nChunkSizeBytes);

    std::size_t nDataSizeBytes = nDataSizeGiB * 1024 * 1024 * 1024;
    bool bAllIterationsSucceeded = true;

    for (std::size_t i = 0; i < nNumIterations; i++)
    {
        if (!RunPerfBlobDirectTest(nNumThreads, nDataSizeBytes, containerClient, strPrefix, strMode, nChunkSizeBytes))
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
    const CacheClientRetryConfig retryConfig{1, 0, 0};
    CacheClientStatus initStatus = client.Initialize(vecCstrHostnames.data(),
                      vecCstrHostnames.size(),
                      nPort,                       /*nCacheServerPort*/
                      false,                       /*bEnableIB*/
                      false,                       /*bEnableCalculateChecksum*/
                      false,                       /*bEnableValidateChecksum*/
                      nullptr,                     /*strChecksumAlgorithm*/
                      vecCstrHostnames.size() * 2, /*nConnectionPoolSize*/
                      false,                       /*bEnableAuth*/
                      nullptr,                     /*strAccountName*/
                      nullptr,                     /*strAccountKey*/
                      false,                       /*bUseLocalServer*/
                      true,                        /*bReuseConnections*/
                      false,                       /*bIsCacheServerDiscoveryEnabled*/
                      300,                         /*nCacheServerDiscoveryIntervalInSeconds*/
                      LOCALHOST,                   /*nCacheServerDiscoveryEndpoint*/
                      true,                        /*bEnableConsistentHashing*/
                      retryConfig);

    if (initStatus != CacheClientStatus::SUCCESS)
    {
        LOG_CRITICAL("Failed to initialize cache client with error {}", initStatus);
        return static_cast<int>(initStatus);
    }

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
    const CacheClientRetryConfig retryConfig{1, 0, 0};
    CacheClientStatus initStatus = client.Initialize(vecCstrHostnames.data(),
                      vecCstrHostnames.size(),
                      nPort,                       /*nCacheServerPort*/
                      false,                       /*bEnableIB*/
                      false,                       /*bEnableCalculateChecksum*/
                      false,                       /*bEnableValidateChecksum*/
                      nullptr,                    /*strChecksumAlgorithm*/
                      vecCstrHostnames.size() * 2, /*nConnectionPoolSize*/
                      false,                       /*bEnableAuth*/
                      nullptr,                     /*strAccountName*/
                      nullptr,                     /*strAccountKey*/
                      false,                       /*bUseLocalServer*/
                      true,                        /*bReuseConnections*/
                      false,                       /*bIsCacheServerDiscoveryEnabled*/
                      300,                         /*nCacheServerDiscoveryIntervalInSeconds*/
                      LOCALHOST,                   /*nCacheServerDiscoveryEndpoint*/
                      true,                        /*bEnableConsistentHashing*/
                      retryConfig);

    if (initStatus != CacheClientStatus::SUCCESS)
    {
        LOG_CRITICAL("Failed to initialize cache client with error {}", initStatus);
        return static_cast<int>(initStatus);
    }

                      
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
enum class ClientMode
{
    PerfClient,
    EvictionClient,
    LastAccessTimeDistribution,
    DirectBlobClient
};

const std::unordered_map<std::string, ClientMode> kClientModeFlags = {
    {"--eviction-client", ClientMode::EvictionClient},
    {"--last-access-time-distribution-client", ClientMode::LastAccessTimeDistribution},
    {"--direct-blob-client", ClientMode::DirectBlobClient},
};
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    LoggingApi::Init();
    LoggingApi::FlushCachedLog();

    if (argc < 2)
    {
        LOG_ERROR("At least one argument is required");
        return 1;
    }

    // Scan all arguments for client mode flags (can appear in any position)
    ClientMode mode = ClientMode::PerfClient;
    std::string selectedFlag;
    for (int i = 1; i < argc; i++)
    {
        auto it = kClientModeFlags.find(argv[i]);
        if (it != kClientModeFlags.end())
        {
            if (!selectedFlag.empty())
            {
                LOG_CRITICAL("Multiple client mode flags specified: '{}' and '{}'. Only one is allowed.", selectedFlag, argv[i]);
                return 1;
            }
            selectedFlag = argv[i];
            mode = it->second;
        }
    }

    switch (mode)
    {
        case ClientMode::EvictionClient:
            return EvictionClient(argc, argv);
        case ClientMode::LastAccessTimeDistribution:
            return LastAccessTimeDistributionClient(argc, argv);
        case ClientMode::DirectBlobClient:
            return PerfDirectBlobClient(argc, argv);
        case ClientMode::PerfClient:
            return PerfClient(argc, argv);
    }
}
//---------------------------------------------------------------------------------------------------------------------
