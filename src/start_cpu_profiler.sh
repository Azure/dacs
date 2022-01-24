#!/bin/bash


# This script is used for on-demand profiling. For on-demand profiling, automated profiling needs to be disabled.
# During on-demand profiling, in the event of a suspend issued when the profiler is turned on, the profiler won't be restarted during restore.
# How to use:
# Set the variables BUILD_TYPE, UNA_RESPONSES_LOCATION, DURATION and trigger the profiler.
# The script triggers a UNA command that can start the profiler, wait for DURATION seconds, then stop the profiler.
# This script is usable only after this PR is merged
# internal-reference-removed


source /repo/src/subsystems/reliability/gpu_unit_tests/core/gpu_unit_test_utils.sh

BUILD_TYPE="Release"
UNA_RESPONSES_LOCATION="/repo/log_fp32_pyramidnet_perf/gpu_unit_test_suite/1.6.0/test_perf_for_pyramidnet_using_elasticity/dacs/job_logs"
DURATION=5
usage="Usage : $0 --job-log-dir <Directory where una_responses.txt can be found> --build-type [Release|Debug] --duration <Profiling duration in seconds>"

while [[ $# -gt 0 ]]
do
    key="$1"
    case $key in
        --build-type)
            shift
            _check_argument_ "$usage" "$1"
            BUILD_TYPE=$1
            shift
            ;;
        --job-log-dir)
            shift
            _check_argument_ "$usage" "$1"
            UNA_RESPONSES_LOCATION="$1"
            shift
            ;;
        --duration)
            shift
            _check_argument_ "$usage" "$1"
            DURATION="$1"
            shift
            ;;
    esac
done

# start cpu profiler
/dacs/bin/$BUILD_TYPE/subsystems/communication/user_node_client/UserNodeCli \
    StartProfiler \
    --log-directory "${UNA_RESPONSES_LOCATION}" \
    --una-address http://`hostname -i`:5002 \
    --profiler-type "CPU"


sleep ${DURATION}s

# stop cpu profiler
/dacs/bin/$BUILD_TYPE/subsystems/communication/user_node_client/UserNodeCli \
    StopProfiler \
    --log-directory "${UNA_RESPONSES_LOCATION}" \
    --una-address http://`hostname -i`:5002 \
    --profiler-type "CPU"


pprof --pdf /usr/bin/python  dacs_cpu_prof_server.prof > /repo/server_call_graph_cpu_py.pdf
pprof --pdf /usr/bin/python  dacs_cpu_prof_client.prof > /repo/client_call_graph_cpu_py.pdf
