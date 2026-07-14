#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
# Ensure this script is sourced, not executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Error: This script should be sourced, not executed directly."
    echo "Usage: source checksum_e2e_test.sh"
    exit 1
fi

function __run_checksum_e2e_test__()
{
    local checksum_algorithm="${1:-xxhash32}"  # Default to crc32 if no parameter provided
    
    if [ "$SKIP_PERF_BENCHMARK" = "true" ]; then
        echo "Skipping checksum performance test as SKIP_PERF_BENCHMARK is set to true"
        return
    fi

    echo "=================================================="
    echo "Checksum Validation & Performance Test (${checksum_algorithm^^})"
    echo "=================================================="

    PERF_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cache-test/PerfClient"
    
    if [[ ! -f "${PERF_CLIENT_EXE}" ]]; then
        echo "ERROR: PerfClient executable not found at: ${PERF_CLIENT_EXE}"
        return 1
    fi

    local timestamp="$(date +%s)"
    local prefix_dis="${checksum_algorithm}-dis-${timestamp}"
    local prefix_en="${checksum_algorithm}-en-${timestamp}"

    echo "Cleaning cache directory before tests..."
    stop_cache_server
    rm -rf ${CACHE_SERVER_PATH}
    mkdir -p ${CACHE_SERVER_PATH}
    start_cache_server /dev/null
    sleep 5

    # ====== VALIDATION DISABLED TEST ======
    echo ""
    echo "1. Upload with ${checksum_algorithm^^} calculate checksum enabled (prefix: ${prefix_dis})"
    eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} \
        --num-threads 8 --data-size-GiB 2 --mode upload --prefix ${prefix_dis} \
        --enable-calculate-checksum true --enable-validate-checksum false --checksum-algorithm ${checksum_algorithm}" > /tmp/upload_${checksum_algorithm}_dis.log
    
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo "ERROR: ${checksum_algorithm^^} upload (validation disabled) failed"
        stop_cache_server
        return 1
    fi

    echo "2. Download with ${checksum_algorithm^^} validate checksum DISABLED"
    eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} \
        --num-threads 8 --data-size-GiB 2 --mode download --prefix ${prefix_dis} \
        --enable-calculate-checksum false --enable-validate-checksum false --checksum-algorithm ${checksum_algorithm}" | tee /tmp/download_${checksum_algorithm}_dis.log
    
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo "ERROR: ${checksum_algorithm^^} download (validation disabled) failed"
        stop_cache_server
        return 1
    fi

    # ====== VALIDATION ENABLED TEST ======
    echo "Cleaning cache directory to free up space..."
    stop_cache_server
    rm -rf ${CACHE_SERVER_PATH}
    mkdir -p ${CACHE_SERVER_PATH}
    start_cache_server /dev/null
    sleep 5  
    
    echo "3. Upload with ${checksum_algorithm^^} calculate checksum enabled (prefix: ${prefix_en})"
    eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} \
        --num-threads 8 --data-size-GiB 2 --mode upload --prefix ${prefix_en} \
        --enable-calculate-checksum true --enable-validate-checksum false --checksum-algorithm ${checksum_algorithm}" > /tmp/upload_${checksum_algorithm}_en.log
    
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo "ERROR: ${checksum_algorithm^^} upload (validation enabled) failed"
        stop_cache_server
        return 1
    fi

    echo "4. Download with ${checksum_algorithm^^} validate checksum ENABLED"
    eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} \
        --num-threads 8 --data-size-GiB 2 --mode download --prefix ${prefix_en} \
        --enable-calculate-checksum false --enable-validate-checksum true --checksum-algorithm ${checksum_algorithm}" | tee /tmp/download_${checksum_algorithm}_en.log
    
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo "ERROR: ${checksum_algorithm^^} download (validation enabled) failed"
        stop_cache_server
        return 1
    fi

    # Extract performance numbers
    local dis_download=$(grep -oP 'Throughput: \K[0-9]+\.?[0-9]*' /tmp/download_${checksum_algorithm}_dis.log | head -1 || echo "0")
    local en_download=$(grep -oP 'Throughput: \K[0-9]+\.?[0-9]*' /tmp/download_${checksum_algorithm}_en.log | head -1 || echo "0")

    echo ""
    echo "=================================================="
    echo "${checksum_algorithm^^} CHECKSUM VALIDATION PERFORMANCE IMPACT"
    echo "=================================================="
    echo "Download (validation disabled): ${dis_download} Mbps"
    echo "Download (validation enabled):  ${en_download} Mbps"
    echo "=================================================="

    stop_cache_server
    return 0
}
