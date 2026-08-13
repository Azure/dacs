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

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
# This script is useful for running the E2E pytorch job with storage interceptor.
# DL job parameters:
# 1. Total number of nodes in the distributed DL job
# 2. Global rank of current node in the distributed task 
#    (This script must be run from all nodes in the distributed job with unique global rank values)
# 3. Number of GPUs to distribute mini-batches within the current node.
# 4. Number of concurrent dataloader workers to use to pre-fetch the next batch inputs into pytorch's data_queue.
# 5. Enable / Disable FP16 (Half Precision) mode for faster compute in V100 GPUs.
# It runs a pytorch job with Distributed Data Parallelism.

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

# ==================================================
# Run DL job
# ==================================================
_run_deep_learning_job()
{
    _NUM_NODES=$1
    _GLOBAL_NODE_RANK=$2
    _NUM_GPUS=$3
    _NUM_DATALOADER_WORKERS=$4
    _ENABLE_FP16=$5
    
    STORAGE_INTERCEPTOR="${REPOSITORY_ROOT}/bin/${BUILD_ARCH}/Debug/native-storage/storage/libStorageIntercept.so"
    runTestCmd="LD_PRELOAD=${STORAGE_INTERCEPTOR} python3 resnet_distributed.py -n ${_NUM_NODES} -nr ${_GLOBAL_NODE_RANK} -g ${_NUM_GPUS} -w ${_NUM_DATALOADER_WORKERS}"

    if [ $_ENABLE_FP16 = true ]
    then
        runTestCmd="${runTestCmd} -fp16 True"
    fi

    eval $runTestCmd
}

# ==================================================
# Help with usage
# ==================================================
_usage()
{
    echo "Run end to end Deep Learning Jobs with Storage Interception"
    echo
    echo "Usage dljobtest.sh [-h] | -n <NUM> -r <NUM> -g <NUM> -w <NUM> [-f]"
    echo "options:"
    echo "-n NUM                Total number of nodes in the distributed training job"
    echo "-r NUM                Global rank of current node in the distributed task (starts with 0)"
    echo "-g NUM                Number of GPUs to distribute mini-batches within the current node"
    echo "-w NUM                Number of concurrent dataloader workers to use to pre-fetch"
    echo "                      the next batch of inputs into pytorch's data_queue."
    echo "-f                    Use FP16 (Half precision) training mode (faster for training with V100s)"
    echo
}

# ==================================================
# Init script variables
# ==================================================
NUM_NODES=1
GLOBAL_NODE_RANK=0
NUM_GPUS=4
NUM_DATALOADER_WORKERS=16
ENABLE_FP16_MODE=false

# ==================================================
# Parse command line args
# ==================================================
while getopts :n:r:g:w:hf flag
do
    case "${flag}" in
        h) _usage;
            exit 0;;
        n) NUM_NODES=${OPTARG};;
        r) GLOBAL_NODE_RANK=${OPTARG};;
        g) NUM_GPUS=${OPTARG};;
        w) NUM_DATALOADER_WORKERS=${OPTARG};;
        f) ENABLE_FP16_MODE=true;;
        *) _usage;
            exit 1;;
    esac
done

# Run the test
echo "Starting the deep learning job with storage interception."
_run_deep_learning_job $NUM_NODES $GLOBAL_NODE_RANK $NUM_GPUS $NUM_DATALOADER_WORKERS $ENABLE_FP16_MODE
echo "Job completed."
