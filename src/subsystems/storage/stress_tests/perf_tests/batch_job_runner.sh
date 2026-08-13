#!/bin/bash
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

############################################################################################
# Run a K8s batch job with increasing parallelism values
# This script will run uploads first, and then downloads with different values of parallelism.
# Update values below before running.
############################################################################################
KUBECONFIG_FILE="$HOME/.kube/config"
LOG_OUTPUT_DIR="$PWD/logs"
FILE_PREFIX="myuniqueprefix"
START_PARALLELISM=3
END_PARALLELISM=6
STEP_PARALLELISM=3
SERVER_LIST="cacheserver-0.cacheserver.default.svc.cluster.local#cacheserver-1.cacheserver.default.svc.cluster.local#cacheserver-2.cacheserver.default.svc.cluster.local#cacheserver-3.cacheserver.default.svc.cluster.local#cacheserver-4.cacheserver.default.svc.cluster.local#cacheserver-5.cacheserver.default.svc.cluster.local#cacheserver-6.cacheserver.default.svc.cluster.local#cacheserver-7.cacheserver.default.svc.cluster.local#cacheserver-8.cacheserver.default.svc.cluster.local#cacheserver-9.cacheserver.default.svc.cluster.local#cacheserver-10.cacheserver.default.svc.cluster.local#cacheserver-11.cacheserver.default.svc.cluster.local#cacheserver-12.cacheserver.default.svc.cluster.local#cacheserver-13.cacheserver.default.svc.cluster.local#cacheserver-14.cacheserver.default.svc.cluster.local#cacheserver-15.cacheserver.default.svc.cluster.local#cacheserver-16.cacheserver.default.svc.cluster.local#cacheserver-17.cacheserver.default.svc.cluster.local#cacheserver-18.cacheserver.default.svc.cluster.local#cacheserver-19.cacheserver.default.svc.cluster.local#cacheserver-20.cacheserver.default.svc.cluster.local#cacheserver-21.cacheserver.default.svc.cluster.local#cacheserver-22.cacheserver.default.svc.cluster.local#cacheserver-23.cacheserver.default.svc.cluster.local#cacheserver-24.cacheserver.default.svc.cluster.local#cacheserver-25.cacheserver.default.svc.cluster.local#cacheserver-26.cacheserver.default.svc.cluster.local#cacheserver-27.cacheserver.default.svc.cluster.local"

# These defaults don't need to be changed in order for the script to run
SERVER_NAME="cacheserver"
DEBUG_MODE="false"
UPLOAD_DATA_ENABLED="true"
DOWNLOAD_JOB_FILE="./download_client.yaml"
DOWNLOAD_JOB_NAME="downloadclient"
UPLOAD_JOB_FILE="./upload_client.yaml"
UPLOAD_JOB_NAME="uploadclient"
############################################################################################
set -e
shopt -s expand_aliases
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"
alias kubectl='kubectl --kubeconfig $KUBECONFIG_FILE'
############################################################################################
function wait_for_job_completion() {
    local job_name="$1"

    while true; do
        completion_time=$(kubectl get job "$job_name" -o jsonpath='{.status.completionTime'})
        if [[ -n "$completion_time" ]]; then
            break
        fi

        echo "Waiting for job $job_name to complete."
        sleep 10
    done

    echo "Job $job_name completed at $completion_time."
}
############################################################################################
function wait_for_server_ready_after_delete() {
    echo "Waiting for server to be ready."
    sleep 5
    while true; do
        ready_replicas=$(kubectl get statefulset $SERVER_NAME -o jsonpath='{.status.readyReplicas}')
        expected_replicas=$(kubectl get statefulset $SERVER_NAME -o jsonpath='{.spec.replicas}')

        if [[ "$ready_replicas" == "$expected_replicas" ]]; then
            break
        fi

        echo "Waiting for server replicas to be ready. $ready_replicas/$expected_replicas ready."
        sleep 10
    done
    echo "Server is ready at $(date)."
}
############################################################################################
function upload_data() {
    if [[ "$UPLOAD_DATA_ENABLED" == "true" ]]; then
        echo "Uploading data to servers"

        kubectl delete job $UPLOAD_JOB_NAME --ignore-not-found=true
        modified_yaml_file="/tmp/output_upload.yaml"
        sed "s/%SERVER_LIST%/$SERVER_LIST/g" "$UPLOAD_JOB_FILE" >"$modified_yaml_file"
        sed -i "s/%FILE_PREFIX%/$FILE_PREFIX/g" "$modified_yaml_file"
        kubectl delete job $UPLOAD_JOB_NAME --ignore-not-found=true
        kubectl apply -f "$modified_yaml_file"
        sleep 5
        wait_for_job_completion $UPLOAD_JOB_NAME
        kubectl logs --selector=app=$UPLOAD_JOB_NAME --tail=-1 >"$LOG_OUTPUT_DIR/upload.log"

        echo "Done uploading data."
        echo "Waiting for snapshot save (300s)"

        sleep 305

        echo "Done waiting for snapshot save."

    else
        echo "Skipping data upload."
    fi
}
############################################################################################
function run_jobs() {
    local restart_before_run="$1"
    local log_suffix="_without_restart"
    if [[ "$restart_before_run" == "restart_before_run" ]]; then
        log_suffix="_with_restart"
    fi

    local csv_path="$LOG_OUTPUT_DIR/summary${log_suffix}.csv"
    echo "Client Parallelism, Total Download Size (bytes), Total Time (ms), Throughput (Mbps)" >>"$csv_path"

    for parallelism in $(seq $START_PARALLELISM $STEP_PARALLELISM $END_PARALLELISM); do
        echo "Running job with parallelism $parallelism."

        kubectl delete job $DOWNLOAD_JOB_NAME --ignore-not-found=true

        if [[ "$restart_before_run" == "restart_before_run" ]]; then
            echo "Restarting servers."
            kubectl delete pod --selector=app=$SERVER_NAME
            wait_for_server_ready_after_delete
        fi

        # Let servers initialize
        sleep 10

        modified_yaml_file="/tmp/output_$parallelism.yaml"
        sed "s/%PARALLELISM%/$parallelism/g" "$DOWNLOAD_JOB_FILE" >"$modified_yaml_file"
        sed -i "s/%SERVER_LIST%/$SERVER_LIST/g" "$modified_yaml_file"
        sed -i "s/%FILE_PREFIX%/$FILE_PREFIX/g" "$modified_yaml_file"

        # Apply the modified YAML file using kubectl
        kubectl apply -f "$modified_yaml_file"
        echo "Applied configuration for parallelism $parallelism." >>"$LOG_OUTPUT_FILE"
        sleep 5
        wait_for_job_completion $DOWNLOAD_JOB_NAME

        kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=-1 >"$LOG_OUTPUT_DIR/$parallelism$log_suffix.log"

        # Custom logic to fetch parallelism and throughput from the logs when running PerfClient.cpp.
        # If some other client is used, this logic needs to be changed.
        echo "==================================" >>"$LOG_OUTPUT_FILE"
        echo "Parallelism: $parallelism" >>"$LOG_OUTPUT_FILE"
        kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=300 | grep "Summary" >>"$LOG_OUTPUT_FILE"
        echo "Throughput Mbps Sum: " | tee -a "$LOG_OUTPUT_FILE"
        tput_Mbps=$(kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=300 | grep "Summary" | awk '{print $NF}' | awk '{s+=$1} END {print s}' | tee -a "$LOG_OUTPUT_FILE")
        echo $tput_Mbps
        echo "==================================" | tee -a "$LOG_OUTPUT_FILE"

        set +e

        total_download_size_bytes=$(kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=300 | grep "Total bytes transferred" | awk '{print $(NF)}' | awk '{s+=$1} END {print s}')
        start_times_ms=$(kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=300 | grep "Summary" | awk '{printf "%s\n" , $6}')
        end_times_ms=$(kubectl logs --selector=app=$DOWNLOAD_JOB_NAME --tail=300 | grep "Summary" | awk '{print "%s\n" , $8}')

        min_start_time=$(echo "$start_times_ms" | sort -n | head -1)
        max_end_time=$(echo "$end_times_ms" | sort -n | tail -1)
        total_time_taken_ms=$(($max_end_time - $min_start_time))
        ((total_time_taken_ms++))
        tput_Mbps=$(echo "scale=2; ($total_download_size_bytes * 8) / ($total_time_taken_ms * 1000)" | bc)

        echo "$parallelism, $total_download_size_bytes, $total_time_taken_ms, $tput_Mbps" >>"$csv_path"

        set -e
    done
}
############################################################################################
if [[ "$DEBUG_MODE" == "true" ]]; then
    set -x
fi
LOG_OUTPUT_DIR=$(realpath "$LOG_OUTPUT_DIR")
mkdir -p "$LOG_OUTPUT_DIR"
LOG_OUTPUT_FILE="$LOG_OUTPUT_DIR/batch_job_runner.log"
touch "$LOG_OUTPUT_FILE"
echo "Logging to $LOG_OUTPUT_FILE"
if [ -z $KUBECONFIG_FILE ] || [ -z $DOWNLOAD_JOB_FILE ] || [ -z $LOG_OUTPUT_FILE ]; then
    echo "Config variables at the top not entered."
    exit 1
fi

echo "Starting at $(date)"

wait_for_server_ready_after_delete

# Upload data
upload_data

# Run with hot os-disk cache
run_jobs

# Run with cold os-disk cache
run_jobs "restart_before_run"

echo "All done. Exiting at $(date)"
