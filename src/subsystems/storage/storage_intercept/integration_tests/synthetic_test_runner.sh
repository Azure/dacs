#!/bin/bash

# This script is an integration test to intercept the I/O related system calls
# made from a test client via LD_PRELOAD and process the request from files present
# in a remote host (Could be a cache or cloud storage services).
# The interception could be configured to run with / without stacking :- . i.e. LfsInterceptor
# already intercepts once, and the storage interceptor would be the next interception layer.

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

STORAGE_INTERCEPTOR="${REPOSITORY_ROOT}/obj/Debug/native/linux-x64/publish/libStorageIntercept.so"
LFS_INTERCEPTOR="${REPOSITORY_ROOT}/obj/Debug/native/linux-x64/publish/libLfsIntercept.so"
PYTHON_IO_TEST="${SCRIPT_DIR}/io_test.py"
EXE_TEST="${REPOSITORY_ROOT}/bin/Debug/native/StorageInterceptIntegTest"

# ==================================================
# Help with usage
# ==================================================
_usage()
{
    echo "Storage intercept integration test with synthetic dataset"
    echo
    echo "Usage synthetictest.sh [-h] | [-s] -d <OPTION> -n <NUM> -p <NUM>"
    echo "options:"
    echo "-s (stacking-enabled) Enables the stacked interception in test, along with LfsInterceptor"
    echo "-d "OPTION" Type of dataset to test against [small | large]"
    echo "-n NUM Number of files to include in the test"
    echo "-p NUM Number of concurrent threads accessing the files in the test"
    echo
}

# ==================================================
# Lfs setup
# ==================================================
_init_lfs_interception()
{
    TEST_ROOT="/tmp/LfsTestDir"
    LOG_DIR="${TEST_ROOT}/Log"
    export DACS_LFS_LOGDIR=${LOG_DIR}
    export DACS_LFS_PPID=$$
}

# ==================================================
# Test runner
# ==================================================
_test_runner()
{
    _STACKING=$1

    # Getting the I/O test properties array
    ioTestArgsGetCmd="declare -A _IO_TEST_ARGS="${2#*=}
    eval $ioTestArgsGetCmd
    _IO_TEST_FOLDER_PATH=${_IO_TEST_ARGS["folder_path"]}
    _IO_TEST_TAR_PATH=${_IO_TEST_ARGS["compressed_tar_path"]}
    _IO_TEST_ZIP_PATH=${_IO_TEST_ARGS["compressed_zip_path"]}

    _SAMPLES_DIR=$3
    _NUM_FILES=$4
    _PARALLELISM=$5

    if [ ${_STACKING} = true ]
    then
        _init_lfs_interception
        STORAGE_INTERCEPT_LD_PRELOAD="LD_PRELOAD=${LFS_INTERCEPTOR}:${STORAGE_INTERCEPTOR}"
    else
        STORAGE_INTERCEPT_LD_PRELOAD="LD_PRELOAD=${STORAGE_INTERCEPTOR}"
    fi

    echo "Starting the python io test"
    runTestCmd="${STORAGE_INTERCEPT_LD_PRELOAD} python3 ${PYTHON_IO_TEST} --folder_path ${_IO_TEST_FOLDER_PATH} --compressed_tar_path ${_IO_TEST_TAR_PATH} --compressed_zip_path ${_IO_TEST_ZIP_PATH}"
    eval $runTestCmd
    echo "Python io test terminated"
    echo

    export STORAGE_INTEG_TESTCASE_FILE=$_SAMPLES_DIR
    runTestCmd="${STORAGE_INTERCEPT_LD_PRELOAD} ${EXE_TEST} ${_NUM_FILES} ${_PARALLELISM}"
    eval $runTestCmd
    unset STORAGE_INTEG_TESTCASE_FILE
}

# ==================================================
# Init script variables
# ==================================================
enable_stacking=false
dataset="small"
num_files_to_test=100
parallelism=4

# ==================================================
# Parse command line args
# ==================================================
while getopts :d:n:p:sh flag
do
    case "${flag}" in
        h) _usage;
            exit 0;;
        d) dataset=${OPTARG};;
        n) num_files_to_test=${OPTARG};;
        p) parallelism=${OPTARG};;
        s) enable_stacking=true;;
        *) _synthetic_test_help; 
            exit 1;;
    esac
done

if [ "$dataset" = "small" ]
then
    samples_file="${SCRIPT_DIR}/small_dataset_testcases.txt"
    declare -A io_test_args=(
        [folder_path]="/mnt/quiverfs/imagenet_data/train/n01443537"
        [compressed_tar_path]="/mnt/quiverfs/n01443537.tar.gz"
        [compressed_zip_path]="/mnt/quiverfs/n01443537.zip")
else
    samples_file="${SCRIPT_DIR}/large_dataset_testcases.txt"
    declare -A io_test_args=(
        [folder_path]="/mnt/quiverfs/books_wiki_en_corpus"
        [compressed_tar_path]="/mnt/quiverfs/books_wiki_en_corpus.tar.gz"
        [compressed_zip_path]="/mnt/quiverfs/books_wiki_en_corpus.zip")
fi

echo "$(readlink -f ${STORAGE_INTERCEPTOR})"
echo "$(readlink -f ${EXE_TEST})"

# Call the test runner
echo "Starting the integration test"
ioTestArgs="declare -p io_test_args"
_test_runner $enable_stacking "$($ioTestArgs)" $samples_file $num_files_to_test $parallelism
echo "Integration test terminated succesfully"
