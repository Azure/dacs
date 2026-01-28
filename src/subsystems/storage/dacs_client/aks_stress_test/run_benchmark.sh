#!/bin/bash
# =============================================================================
# Throughput Benchmark Script for DACS Client
# =============================================================================
# Usage: ./run_benchmark.sh [NUM_NODES] [STORAGE_ACCOUNT] [NUM_PROCESSES] [NUM_BLOBS] [BLOB_SIZE_GB] [WRITE_MODE]
# 
# This script validates that ALL data is successfully written and read before
# calculating throughput. Partial completions are reported as FAILED.
#
# Throughput Calculation:
#   - MB/s: Sum of each node's individual throughput (data/duration per node)
#   - GB/s: MB/s / 1024
#   - Gbps: MB/s * 8 / 1000 (converting MegaBytes to Gigabits)
# =============================================================================

set -o pipefail

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

# Expected data per node in GB
EXPECTED_DATA_PER_NODE_GB=$((NUM_PROCESSES * NUM_BLOBS * BLOB_SIZE_GB))

# Get list of running pods
ALL_PODS=$(kubectl get pods -l app=dacs-client -o jsonpath='{.items[*].metadata.name}' | tr ' ' '\n')
TOTAL_PODS=$(echo "$ALL_PODS" | wc -l)

# Select number of nodes
if [ "$NUM_NODES" = "all" ]; then
    PODS="$ALL_PODS"
    NUM_NODES=$TOTAL_PODS
else
    PODS=$(echo "$ALL_PODS" | head -n "$NUM_NODES")
fi

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
echo "======================================================================" 
echo ""

# Run benchmarks in parallel on all nodes
echo "Starting benchmarks on all nodes..."
PIDS=""
i=1
for pod in $PODS; do
    echo "  Starting benchmark on $pod (node $i)..."
    kubectl exec "$pod" -- bash -c "export STORAGE_INTCPT_SAS_TOKEN='${SAS_TOKEN}' && cd /app && python3 -m py_dacs_client.benchmarks.throughput_benchmark --storage-account $STORAGE_ACCOUNT --num-processes $NUM_PROCESSES --num-blobs $NUM_BLOBS --blob-size-gb $BLOB_SIZE_GB --write-mode $WRITE_MODE --node-id node${i}" > /tmp/benchmark_results/node${i}.txt 2>&1 &
    PIDS="$PIDS $!"
    i=$((i+1))
done

echo ""
echo "Waiting for all benchmarks to complete..."
wait $PIDS
echo "All benchmarks completed!"
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
    
    # Check if FINAL SUMMARY exists (benchmark completed)
    if ! grep -q "FINAL SUMMARY" "$result"; then
        echo "  ✗ BENCHMARK FAILED - No final summary found"
        echo "  Last 10 lines:"
        tail -10 "$result" | sed 's/^/    /'
        failed_nodes="$failed_nodes $node_num"
        continue
    fi
    
    # Extract the FINAL SUMMARY section (use grep with context instead of awk to avoid pattern issues)
    grep -A20 "FINAL SUMMARY" "$result" | head -20
    
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
    
    # Extract write data from FINAL SUMMARY (format: "  Total Data:    224.00 GB")
    write_data_gb=$(grep -A20 "FINAL SUMMARY" "$result" | grep -A5 "^WRITE:" | grep "Total Data:" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    
    # Extract write throughput (format: "  Throughput:    1744 MB/s | 1.70 GB/s")
    write_throughput_mbs=$(grep -A20 "FINAL SUMMARY" "$result" | grep -A5 "^WRITE:" | grep "Throughput:" | grep -oE '[0-9]+ MB/s' | grep -oE '[0-9]+')
    
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
    
    # Extract read data from FINAL SUMMARY
    read_data_gb=$(grep -A20 "FINAL SUMMARY" "$result" | grep -A5 "^READ:" | grep "Total Data:" | grep -oE '[0-9]+\.[0-9]+' | head -1)
    
    # Extract read throughput
    read_throughput_mbs=$(grep -A20 "FINAL SUMMARY" "$result" | grep -A5 "^READ:" | grep "Throughput:" | grep -oE '[0-9]+ MB/s' | grep -oE '[0-9]+')
    
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
