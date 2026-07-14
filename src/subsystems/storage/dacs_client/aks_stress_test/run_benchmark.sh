#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# =============================================================================
# Throughput Benchmark Script for DACS Client
# =============================================================================
# Usage: ./run_benchmark.sh [NUM_NODES] [STORAGE_ACCOUNT] [NUM_PROCESSES] [NUM_BLOBS] [BLOB_SIZE_GB] [WRITE_MODE] [CLIENTS]
# 
# This script validates that ALL data is successfully written and read before
# calculating throughput. Partial completions are reported as FAILED.
#
# Throughput Calculation:
#   - MB/s: Sum of each node's individual throughput (data/duration per node)
#   - GB/s: MB/s / 1024
#   - Gbps: MB/s * 8 / 1000 (converting MegaBytes to Gigabits)
#
# Environment Variables:
#   Required for SAS auth:
#     - SAS_TOKEN
#
#   Automatically exported to benchmark processes by this script:
#     - STORAGE_INTCPT_SAS_TOKEN (set from SAS_TOKEN)
#     - USE_SAS_TOKEN=true
#
#   Cache behavior:
#     - WRITE_MODE=write_blob_only disables cache for benchmark processes
#     - For other write modes, cache is enabled and this script auto-discovers
#       cache server pods across namespaces and overrides CACHE_SERVER_LIST with
#       resolved cache pod IPs.
#
#   Optional cache override inputs (if needed for custom environments):
#     - CACHE_SERVER_LIST
#     - CACHE_SERVER_PORT
#     - CACHE_DISCOVERY_URL
# =============================================================================

set -o pipefail

show_help() {
        cat << 'EOF'
Usage: ./run_benchmark.sh [NUM_NODES] [STORAGE_ACCOUNT] [NUM_PROCESSES] [NUM_BLOBS] [BLOB_SIZE_GB] [WRITE_MODE] [CLIENTS]

Arguments:
    NUM_NODES       Number of dacs-client pods to use, or 'all' (default: all)
    STORAGE_ACCOUNT Storage account name (default: storageintercepttestae)
    NUM_PROCESSES   Processes per node (default: 16)
    NUM_BLOBS       Blobs per process (default: 2)
    BLOB_SIZE_GB    Blob size in GB (default: 7)
    WRITE_MODE      Write mode (default: write_blob_only)
    CLIENTS         Client mode (default: dacs_zero_copy)

Environment Variables:
    Required for SAS auth:
        - SAS_TOKEN

    Automatically exported to benchmark processes by this script:
        - STORAGE_INTCPT_SAS_TOKEN (set from SAS_TOKEN)
        - USE_SAS_TOKEN=true

    Cache behavior:
        - WRITE_MODE=write_blob_only disables cache for benchmark processes
        - For other write modes, cache is enabled and this script auto-discovers
            cache server pods across namespaces and overrides CACHE_SERVER_LIST with
            resolved cache pod IPs.

    Optional cache override inputs (if needed for custom environments):
        - CACHE_SERVER_LIST
        - CACHE_SERVER_PORT
        - CACHE_DISCOVERY_URL
EOF
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        show_help
        exit 0
fi

# SAS_TOKEN must be set as environment variable before running this script
if [ -z "$SAS_TOKEN" ]; then
    echo "ERROR: SAS_TOKEN environment variable is not set."
    echo ""
    echo "Please set it before running the benchmark:"
    echo "  export SAS_TOKEN='sp=racwdl&st=...&sig=...'"
    echo ""
    exit 1
fi

NUM_NODES="${1:-all}"
STORAGE_ACCOUNT="${2:-storageintercepttestae}"
NUM_PROCESSES="${3:-16}"
NUM_BLOBS="${4:-2}"
BLOB_SIZE_GB="${5:-7}"
WRITE_MODE="${6:-write_blob_only}"
CLIENTS="${7:-dacs_zero_copy}"

# Expected data per node in GB
EXPECTED_DATA_PER_NODE_GB=$((NUM_PROCESSES * NUM_BLOBS * BLOB_SIZE_GB))

# Get list of running dacs-client pods across all namespaces
ALL_PODS=$(kubectl get pods -A -l app=dacs-client --field-selector=status.phase=Running -o jsonpath='{range .items[*]}{.metadata.namespace}/{.metadata.name}{"\n"}{end}')
TOTAL_PODS=$(echo "$ALL_PODS" | sed '/^$/d' | wc -l)

if [ "$TOTAL_PODS" -eq 0 ]; then
    echo "ERROR: No running dacs-client pods found in any namespace."
    exit 1
fi

# Select number of nodes
if [ "$NUM_NODES" = "all" ]; then
    PODS="$ALL_PODS"
    NUM_NODES=$TOTAL_PODS
else
    PODS=$(echo "$ALL_PODS" | sed '/^$/d' | head -n "$NUM_NODES")
    NUM_NODES=$(echo "$PODS" | sed '/^$/d' | wc -l)
fi

# Configure retry settings based on number of nodes
# More nodes = more concurrent requests = higher throttling probability
# Scale up retries and delays accordingly
if [ "$NUM_NODES" -le 5 ]; then
    # Low load: minimal retries
    BLOB_RETRY_MAX_ATTEMPTS=10
    BLOB_RETRY_BASE_DELAY_SECONDS=1
    BLOB_RETRY_MAX_DELAY_SECONDS=64
elif [ "$NUM_NODES" -le 10 ]; then
    # Medium load: moderate retries
    BLOB_RETRY_MAX_ATTEMPTS=15
    BLOB_RETRY_BASE_DELAY_SECONDS=2
    BLOB_RETRY_MAX_DELAY_SECONDS=128
elif [ "$NUM_NODES" -le 20 ]; then
    # High load: aggressive retries
    BLOB_RETRY_MAX_ATTEMPTS=20
    BLOB_RETRY_BASE_DELAY_SECONDS=3
    BLOB_RETRY_MAX_DELAY_SECONDS=256
else
    # Very high load (20+ nodes): maximum retries
    BLOB_RETRY_MAX_ATTEMPTS=25
    BLOB_RETRY_BASE_DELAY_SECONDS=4
    BLOB_RETRY_MAX_DELAY_SECONDS=300
fi

# Evict cache on all cache servers before the run
# This ensures each benchmark starts with a clean cache and prevents capacity issues
evict_cache() {
    echo "Evicting cache on all cache servers..."
    CACHE_SERVERS=$(kubectl get pods -A -l app=cacheserver -o jsonpath='{range .items[*]}{.metadata.namespace}/{.metadata.name}{"\n"}{end}' 2>/dev/null)
    if [ -z "$CACHE_SERVERS" ]; then
        echo "  No cache server pods found, skipping eviction"
        return
    fi
    for srv_ref in $CACHE_SERVERS; do
        srv_ns="${srv_ref%%/*}"
        srv_name="${srv_ref#*/}"
        echo -n "  ${srv_ns}/${srv_name}: "
        # Delete all cached data files and snapshot to free disk
        kubectl -n "$srv_ns" exec "$srv_name" -- bash -c 'rm -rf /host/mnt/ssd0/cache/_cache/* /host/mnt/ssd0/cache/snapshot/*' 2>/dev/null
        echo "files cleared"
    done
    # Rolling restart to clear in-memory cache state (metadata must match disk)
    echo "  Restarting cache server pods..."
    CACHE_NAMESPACES=$(echo "$CACHE_SERVERS" | sed '/^$/d' | cut -d'/' -f1 | sort -u)
    for cache_ns in $CACHE_NAMESPACES; do
        kubectl -n "$cache_ns" rollout restart statefulset/cacheserver 2>/dev/null
        kubectl -n "$cache_ns" rollout status statefulset/cacheserver --timeout=300s 2>/dev/null
    done
    echo "  Cache eviction complete."
    echo ""
}

evict_cache

# Clear previous results
rm -rf /tmp/benchmark_results
mkdir -p /tmp/benchmark_results

echo "======================================================================" 
echo "MULTI-NODE BENCHMARK"
echo "======================================================================" 
echo "Storage Account: $STORAGE_ACCOUNT"
echo "Nodes: $NUM_NODES (of $TOTAL_PODS available)"
echo "Processes per node: $NUM_PROCESSES"
echo "Blobs per process: $NUM_BLOBS"
echo "Blob size: ${BLOB_SIZE_GB} GB"
echo "Expected data per node: ${EXPECTED_DATA_PER_NODE_GB} GB"
echo "Write mode: $WRITE_MODE"
echo ""
echo "Retry Configuration (based on $NUM_NODES nodes):"
echo "  Max Attempts:    $BLOB_RETRY_MAX_ATTEMPTS"
echo "  Base Delay:      ${BLOB_RETRY_BASE_DELAY_SECONDS}s"
echo "  Max Delay:       ${BLOB_RETRY_MAX_DELAY_SECONDS}s"
echo "  Clients:         $CLIENTS"
echo "======================================================================" 
echo ""

# For write_blob_only mode, disable cache to avoid unnecessary memory overhead
# (cache server threads, upload buffers, connection pools waste ~3GB per process)
CACHE_OVERRIDE=""
if [ "$WRITE_MODE" = "write_blob_only" ]; then
    CACHE_OVERRIDE="unset CACHE_SERVER_LIST && unset CACHE_SERVER_PORT && "
    echo "Note: Cache disabled for write_blob_only mode (no cache needed for blob-only operations)"
else
    # Pre-resolve cache server DNS names to IPs to avoid DNS resolution failures
    # under high concurrency (32 procs × N nodes all resolving 8 DNS names simultaneously
    # can overwhelm CoreDNS). We resolve once here and override CACHE_SERVER_LIST with IPs.
    echo "Pre-resolving cache server IPs..."
    CACHE_SERVER_IPS=""
    CACHE_PODS=$(kubectl get pods -A -l app=cacheserver -o jsonpath='{range .items[*]}{.metadata.namespace}/{.metadata.name}{"\n"}{end}' 2>/dev/null | sort)
    for cpod_ref in $CACHE_PODS; do
        cpod_ns="${cpod_ref%%/*}"
        cpod_name="${cpod_ref#*/}"
        IP=$(kubectl -n "$cpod_ns" get pod "$cpod_name" -o jsonpath='{.status.podIP}' 2>/dev/null)
        if [ -n "$IP" ]; then
            if [ -n "$CACHE_SERVER_IPS" ]; then
                CACHE_SERVER_IPS="${CACHE_SERVER_IPS}#${IP}"
            else
                CACHE_SERVER_IPS="${IP}"
            fi
            echo "  ${cpod_ns}/${cpod_name} -> $IP"
        else
            echo "  WARNING: Could not resolve IP for ${cpod_ns}/${cpod_name}"
        fi
    done
    if [ -n "$CACHE_SERVER_IPS" ]; then
        CACHE_OVERRIDE="export CACHE_SERVER_LIST='${CACHE_SERVER_IPS}' && "
        echo "  CACHE_SERVER_LIST set to: $CACHE_SERVER_IPS"
    else
        echo "  WARNING: No cache server IPs resolved, using daemonset defaults"
    fi
    echo ""
fi

# =============================================================================
# HELPER FUNCTIONS for launching, verifying, and polling benchmarks
# =============================================================================

LAUNCH_DELAY=2  # seconds between each kubectl exec launch

# Launch benchmark on all pods with given extra args
# Args: $1=extra_benchmark_args  $2=output_file_on_pod  $3=phase_label
launch_on_pods() {
    local extra_args="$1"
    local output_file="$2"
    local phase_label="$3"

    echo "Starting ${phase_label} on all nodes..."
    local i=1
    for pod_ref in $PODS; do
        pod_ns="${pod_ref%%/*}"
        pod_name="${pod_ref#*/}"
        echo "  Starting on ${pod_ns}/${pod_name} (node $i)..."
        SCRIPT_CONTENT="#!/bin/bash
${CACHE_OVERRIDE}
export STORAGE_INTCPT_SAS_TOKEN='${SAS_TOKEN}'
export USE_SAS_TOKEN=true
export SI_blobRetryMaxAttempts=${BLOB_RETRY_MAX_ATTEMPTS}
export SI_blobRetryBaseDelaySeconds=${BLOB_RETRY_BASE_DELAY_SECONDS}
export SI_blobRetryMaxDelaySeconds=${BLOB_RETRY_MAX_DELAY_SECONDS}
cd /app && python3 -m py_dacs_client.benchmarks.throughput_benchmark \
    --storage-account ${STORAGE_ACCOUNT} \
    --num-processes ${NUM_PROCESSES} \
    --num-blobs ${NUM_BLOBS} \
    --blob-size-gb ${BLOB_SIZE_GB} \
    --clients ${CLIENTS} \
    --write-mode ${WRITE_MODE} \
    --node-id node${i} ${extra_args}
"
        B64=$(echo "$SCRIPT_CONTENT" | base64 -w0)
        LAUNCH_OUTPUT=$(timeout 15 kubectl -n "$pod_ns" exec "$pod_name" -- bash -c "echo $B64 | base64 -d > /tmp/run_bench.sh && chmod +x /tmp/run_bench.sh && bash /tmp/run_bench.sh > ${output_file} 2>&1 </dev/null & disown; echo LAUNCHED" 2>&1)
        if echo "$LAUNCH_OUTPUT" | grep -q "LAUNCHED"; then
            echo "    ✓ Launched successfully"
        else
            echo "    ✗ Failed to launch on ${pod_ns}/${pod_name}: $LAUNCH_OUTPUT"
        fi
        i=$((i+1))
        sleep $LAUNCH_DELAY
    done
}

# Verify all nodes have started producing output
# Args: $1=output_file_on_pod  $2=phase_label
verify_io_started() {
    local output_file="$1"
    local phase_label="$2"

    echo ""
    echo "Verifying all nodes started ${phase_label}..."
    local VERIFY_INTERVAL=10
    local VERIFY_MAX_WAIT=300
    local VERIFY_WAIT=0
    while true; do
        local all_started=true
        local not_started_list=""
        local started_count=0
        local i=1
        for pod_ref in $PODS; do
            pod_ns="${pod_ref%%/*}"
            pod_name="${pod_ref#*/}"
            has_output=$(timeout 10 kubectl -n "$pod_ns" exec "$pod_name" -- bash -c "if [ -f ${output_file} ] && [ -s ${output_file} ]; then echo 1; else echo 0; fi" 2>/dev/null || echo "0")
            if [ "$has_output" != "1" ]; then
                all_started=false
                not_started_list="${not_started_list} node${i}(${pod_ns}/${pod_name})"
            else
                started_count=$((started_count + 1))
            fi
            i=$((i+1))
        done

        if $all_started; then
            echo "  ✓ All $NUM_NODES nodes confirmed producing output"
            break
        fi

        VERIFY_WAIT=$((VERIFY_WAIT + VERIFY_INTERVAL))
        if [ $VERIFY_WAIT -ge $VERIFY_MAX_WAIT ]; then
            echo "  ⚠ WARNING: After ${VERIFY_MAX_WAIT}s, some nodes have NOT started:${not_started_list}"
            echo "  Continuing to poll anyway..."
            break
        fi

        echo "  [${VERIFY_WAIT}s] ${started_count}/${NUM_NODES} nodes started. Waiting for:${not_started_list}"
        sleep $VERIFY_INTERVAL
    done
}

# Poll pods until all complete (SUMMARY: marker found), saving results locally
# Args: $1=output_file_on_pod  $2=phase_label  $3=local_result_prefix (e.g. /tmp/benchmark_results/node)
poll_completion() {
    local output_file="$1"
    local phase_label="$2"
    local result_prefix="$3"

    echo ""
    echo "Polling for ${phase_label} completion..."
    echo "(Each node processes ~${EXPECTED_DATA_PER_NODE_GB} GB - this may take a while)"
    echo ""

    local COMPLETED_NODES=""
    local TOTAL_WAIT=0
    local POLL_INTERVAL=30
    local MAX_WAIT=7200

    while true; do
        local all_done=true
        local i=1
        for pod_ref in $PODS; do
            pod_ns="${pod_ref%%/*}"
            pod_name="${pod_ref#*/}"
            if echo "$COMPLETED_NODES" | grep -q "node${i} "; then
                i=$((i+1))
                continue
            fi
            result=$(kubectl -n "$pod_ns" exec "$pod_name" -- bash -c "grep -c '^SUMMARY:' ${output_file} 2>/dev/null || echo 0" 2>/dev/null)
            if [ "$result" = "1" ]; then
                echo "  [${TOTAL_WAIT}s] Node $i (${pod_ns}/${pod_name}) - COMPLETED"
                kubectl -n "$pod_ns" exec "$pod_name" -- cat "${output_file}" > "${result_prefix}${i}.txt" 2>/dev/null
                COMPLETED_NODES="${COMPLETED_NODES}node${i} "
            else
                all_done=false
            fi
            i=$((i+1))
        done

        if $all_done; then
            echo ""
            echo "${phase_label} completed on all nodes!"
            break
        fi

        TOTAL_WAIT=$((TOTAL_WAIT + POLL_INTERVAL))
        if [ $TOTAL_WAIT -ge $MAX_WAIT ]; then
            echo ""
            echo "WARNING: Timed out after ${MAX_WAIT}s for ${phase_label}. Collecting partial results..."
            i=1
            for pod_ref in $PODS; do
                pod_ns="${pod_ref%%/*}"
                pod_name="${pod_ref#*/}"
                if ! echo "$COMPLETED_NODES" | grep -q "node${i} "; then
                    kubectl -n "$pod_ns" exec "$pod_name" -- cat "${output_file}" > "${result_prefix}${i}.txt" 2>/dev/null
                fi
                i=$((i+1))
            done
            break
        fi

        completed_count=$(echo "$COMPLETED_NODES" | wc -w)
        echo "  [${TOTAL_WAIT}s] ${completed_count}/${NUM_NODES} nodes completed. Waiting..."
        sleep $POLL_INTERVAL
    done
}

# =============================================================================
# ORCHESTRATE BENCHMARK
# =============================================================================

if [ "$WRITE_MODE" = "write_blob_only" ]; then
    # ------------------------------------------------------------------
    # SINGLE-PHASE: write+read together (no cache involved)
    # ------------------------------------------------------------------
    launch_on_pods "" "/tmp/benchmark_output.txt" "benchmark"
    verify_io_started "/tmp/benchmark_output.txt" "IO"
    poll_completion "/tmp/benchmark_output.txt" "Full benchmark" "/tmp/benchmark_results/node"
else
    # ------------------------------------------------------------------
    # TWO-PHASE: write-only → global barrier → read-only
    # Ensures cache is fully populated before any node starts reading,
    # guaranteeing 100% cache hits during the read phase.
    # ------------------------------------------------------------------
    echo "======================================================================"
    echo "TWO-PHASE MODE for cache-backed write mode ($WRITE_MODE)"
    echo "  Phase 1: Write-only (all nodes write, populating cache)"
    echo "  Barrier: Wait for ALL writes to finish"
    echo "  Phase 2: Read-only  (all nodes read from fully-populated cache)"
    echo "======================================================================"
    echo ""

    # --- Phase 1: WRITE ---
    # --no-cleanup: Keep blobs for the read phase (cleanup happens after reads)
    launch_on_pods "--skip-read --no-cleanup --paths-file /tmp/bench_paths.json" \
                   "/tmp/benchmark_write_output.txt" "WRITE phase"
    verify_io_started "/tmp/benchmark_write_output.txt" "write IO"
    poll_completion "/tmp/benchmark_write_output.txt" "Write phase" "/tmp/benchmark_results/write_node"

    echo ""
    echo "======================================================================"
    echo "GLOBAL BARRIER — All writes complete. Cache fully populated."
    echo "Starting read phase for 100% cache reads..."
    echo "======================================================================"
    echo ""

    # --- Phase 2: READ ---
    launch_on_pods "--skip-write --paths-file /tmp/bench_paths.json" \
                   "/tmp/benchmark_read_output.txt" "READ phase"
    verify_io_started "/tmp/benchmark_read_output.txt" "read IO"
    poll_completion "/tmp/benchmark_read_output.txt" "Read phase" "/tmp/benchmark_results/read_node"

    # Combine write + read outputs per node so the result parser can find both sections
    for i in $(seq 1 $NUM_NODES); do
        cat "/tmp/benchmark_results/write_node${i}.txt" \
            "/tmp/benchmark_results/read_node${i}.txt" \
            > "/tmp/benchmark_results/node${i}.txt" 2>/dev/null
    done
fi

echo ""

# Parse and summarize results
echo "======================================================================"
echo "INDIVIDUAL NODE RESULTS"
echo "======================================================================"

total_write_throughput_mbs=0
total_read_throughput_mbs=0
write_success_count=0
read_success_count=0
failed_nodes=""

for result in /tmp/benchmark_results/node*.txt; do
    node_num=$(basename "$result" .txt)
    echo ""
    echo "--- $node_num ---"
    
    # Check if SUMMARY section exists (benchmark completed)
    if ! grep -q "^SUMMARY:" "$result"; then
        echo "  ✗ BENCHMARK FAILED - No final summary found"
        echo "  Last 10 lines:"
        tail -10 "$result" | sed 's/^/    /'
        failed_nodes="$failed_nodes $node_num"
        continue
    fi
    
    # Extract the SUMMARY section
    grep -A20 "^SUMMARY:" "$result" | head -20
    
    # --------------- WRITE VALIDATION ---------------
    # Check write processes succeeded (format: "Processes Succeeded: 16/16")
    write_procs_line=$(grep "WRITE RESULTS" -A10 "$result" | grep "Processes Succeeded:" | head -1)
    if [ -n "$write_procs_line" ]; then
        write_succeeded=$(echo "$write_procs_line" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f1)
        write_total=$(echo "$write_procs_line" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f2)
    else
        write_succeeded=0
        write_total=$NUM_PROCESSES
    fi
    
    # Extract write data from SUMMARY section (format: "  Total Data:    224.00 GB")
    write_data_gb=$(grep -A20 "^SUMMARY:" "$result" | grep -A5 "^WRITE:" | grep "Total Data:" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    
    # Extract write throughput (format: "  Throughput:    1744 MB/s | 1.70 GB/s")
    write_throughput_mbs=$(grep -A20 "^SUMMARY:" "$result" | grep -A5 "^WRITE:" | grep "Throughput:" | grep -oE '[0-9]+ MB/s' | grep -oE '[0-9]+')
    
    # Validate write completeness
    write_ok=0
    if [ -n "$write_data_gb" ] && [ -n "$write_throughput_mbs" ]; then
        write_data_int=${write_data_gb%.*}
        if [ "$write_succeeded" = "$write_total" ] && [ "$write_data_int" -ge "$EXPECTED_DATA_PER_NODE_GB" ]; then
            total_write_throughput_mbs=$((total_write_throughput_mbs + write_throughput_mbs))
            write_success_count=$((write_success_count + 1))
            write_ok=1
        else
            echo "  *** WRITE INCOMPLETE: Processes $write_succeeded/$write_total, Data ${write_data_gb:-0} GB (expected ${EXPECTED_DATA_PER_NODE_GB} GB) ***"
        fi
    else
        echo "  *** WRITE: No data found in output ***"
    fi
    
    # --------------- READ VALIDATION ---------------
    # Check read processes succeeded
    read_procs_line=$(grep "READ RESULTS" -A10 "$result" | grep "Processes Succeeded:" | head -1)
    if [ -n "$read_procs_line" ]; then
        read_succeeded=$(echo "$read_procs_line" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f1)
        read_total=$(echo "$read_procs_line" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f2)
    else
        read_succeeded=0
        read_total=$NUM_PROCESSES
    fi
    
    # Extract read data from SUMMARY section
    read_data_gb=$(grep -A20 "^SUMMARY:" "$result" | grep -A5 "^READ:" | grep "Total Data:" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    
    # Extract read throughput
    read_throughput_mbs=$(grep -A20 "^SUMMARY:" "$result" | grep -A5 "^READ:" | grep "Throughput:" | grep -oE '[0-9]+ MB/s' | grep -oE '[0-9]+')
    
    # Validate read completeness
    read_ok=0
    if [ -n "$read_data_gb" ] && [ -n "$read_throughput_mbs" ]; then
        read_data_int=${read_data_gb%.*}
        if [ "$read_succeeded" = "$read_total" ] && [ "$read_data_int" -ge "$EXPECTED_DATA_PER_NODE_GB" ]; then
            total_read_throughput_mbs=$((total_read_throughput_mbs + read_throughput_mbs))
            read_success_count=$((read_success_count + 1))
            read_ok=1
        else
            echo "  *** READ INCOMPLETE: Processes $read_succeeded/$read_total, Data ${read_data_gb:-0} GB (expected ${EXPECTED_DATA_PER_NODE_GB} GB) ***"
        fi
    else
        echo "  *** READ: No data found in output (read may have been skipped or failed) ***"
    fi
    
    # Track failed nodes
    if [ $write_ok -eq 0 ] || [ $read_ok -eq 0 ]; then
        failed_nodes="$failed_nodes $node_num"
    fi
done

echo ""
echo "======================================================================"
echo "COMBINED SUMMARY"
echo "======================================================================"
echo ""
echo "Configuration:"
echo "  Nodes Requested:      $NUM_NODES"
echo "  Processes per Node:   $NUM_PROCESSES"
echo "  Expected Data/Node:   ${EXPECTED_DATA_PER_NODE_GB} GB"
echo "  Total Expected Data:  $((EXPECTED_DATA_PER_NODE_GB * NUM_NODES)) GB"
echo ""
echo "Results:"
echo "  Write Nodes Complete: $write_success_count / $NUM_NODES"
echo "  Read Nodes Complete:  $read_success_count / $NUM_NODES"
echo ""

# Calculate and display throughput
# Formula: Gbps = MB/s * 8 bits/byte / 1000 (to convert Mb to Gb)
#          GB/s = MB/s / 1024

echo "----------------------------------------------------------------------"
if [ "$write_success_count" -eq "$NUM_NODES" ]; then
    write_gbs=$(awk "BEGIN {printf \"%.2f\", $total_write_throughput_mbs / 1024}")
    write_gbps=$(awk "BEGIN {printf \"%.2f\", $total_write_throughput_mbs * 8 / 1000}")
    echo "WRITE: ✓ ALL $NUM_NODES NODES COMPLETED (100% success)"
    echo "  Combined Throughput: $total_write_throughput_mbs MB/s"
    echo "                       $write_gbs GB/s"
    echo "                       $write_gbps Gbps"
elif [ "$write_success_count" -gt 0 ]; then
    echo "WRITE: ⚠ PARTIAL ($write_success_count/$NUM_NODES nodes completed)"
    echo "  Partial Throughput:  $total_write_throughput_mbs MB/s (NOT RELIABLE - incomplete data)"
else
    echo "WRITE: ✗ FAILED (0/$NUM_NODES nodes completed)"
fi
echo ""

if [ "$read_success_count" -eq "$NUM_NODES" ]; then
    read_gbs=$(awk "BEGIN {printf \"%.2f\", $total_read_throughput_mbs / 1024}")
    read_gbps=$(awk "BEGIN {printf \"%.2f\", $total_read_throughput_mbs * 8 / 1000}")
    echo "READ:  ✓ ALL $NUM_NODES NODES COMPLETED (100% success)"
    echo "  Combined Throughput: $total_read_throughput_mbs MB/s"
    echo "                       $read_gbs GB/s"
    echo "                       $read_gbps Gbps"
elif [ "$read_success_count" -gt 0 ]; then
    echo "READ:  ⚠ PARTIAL ($read_success_count/$NUM_NODES nodes completed)"
    echo "  Partial Throughput:  $total_read_throughput_mbs MB/s (NOT RELIABLE - incomplete data)"
else
    echo "READ:  ✗ FAILED (0/$NUM_NODES nodes completed)"
fi
echo "----------------------------------------------------------------------"
echo ""
echo "======================================================================"

# Exit with error if not all nodes succeeded
if [ -n "$failed_nodes" ]; then
    echo ""
    echo "*** BENCHMARK INCOMPLETE ***"
    echo "Failed/Incomplete nodes:$failed_nodes"
    echo ""
    echo "To investigate, check individual node logs:"
    echo "  cat /tmp/benchmark_results/nodeX.txt"
    exit 1
fi

echo ""
echo "✓ BENCHMARK COMPLETED SUCCESSFULLY"
echo "  All $NUM_NODES nodes wrote and read ${EXPECTED_DATA_PER_NODE_GB} GB each"
echo "  Total data transferred: $((EXPECTED_DATA_PER_NODE_GB * NUM_NODES * 2)) GB (write + read)"
