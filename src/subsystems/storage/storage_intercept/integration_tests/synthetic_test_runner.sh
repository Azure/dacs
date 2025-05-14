#!/bin/bash

# This script is an integration test to intercept the I/O related system calls
# made from a test client via LD_PRELOAD and process the request from files present
# in a remote host (Could be a cache or cloud storage services).
# The interception could be configured to run with / without stacking :- . i.e. LfsInterceptor
# already intercepts once, and the storage interceptor would be the next interception layer.

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

# ==================================================
# Help with usage
# ==================================================
_usage()
{
    echo "Storage intercept integration test with synthetic dataset"
    echo
    echo "Usage synthetictest.sh -b Debug|Release [-h] | [-s] -d <OPTION> -n <NUM> -p <NUM>"
    echo "options:"
    echo "-b Debug|Release Build configuration to use"
    echo "-s (stacking-enabled) Enables the stacked interception in test, along with LfsInterceptor"
    echo "-d "OPTION" Type of dataset to test against [small | large]"
    echo "-n NUM Number of files to include in the test"
    echo "-p NUM Number of concurrent threads accessing the files in the test"
    echo "-f Use fuse version of storageinterception"
    echo "-e Enable discovery"
    echo "-h Print this Help."
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

    _MOUNT_PATH=$3
    _SAMPLES_DIR=$4
    _NUM_FILES=$5
    _PARALLELISM=$6
    _USE_FUSE=$7

    if [ ${_STACKING} = true ]
    then
        _init_lfs_interception
        STORAGE_INTERCEPT_LD_PRELOAD="LD_PRELOAD=${LFS_INTERCEPTOR}:${STORAGE_INTERCEPTOR}"
    else
        STORAGE_INTERCEPT_LD_PRELOAD="LD_PRELOAD=${STORAGE_INTERCEPTOR}"
    fi
    unset DACS_LOG_OUTPUT_FILE
    unset DACS_LOG_ERROR_FILE

    if [ ${_USE_FUSE} = true ]
    then
        STORAGE_INTERCEPT_LD_PRELOAD=""
        export DACS_LOG_OUTPUT_FILE="${REPOSITORY_ROOT}/dacsfs.log"
        export DACS_LOG_ERROR_FILE="${REPOSITORY_ROOT}/dacsfs.err"

        STORAGE_FUSE_MOUNT="${REPOSITORY_ROOT}/bin/${build_configuration}/native-storage/storage/dacsfs"

        echo "Doing mount1"
        umount /mnt/quiverfs
        mkdir -p /mnt/quiverfs
        echo "$STORAGE_FUSE_MOUNT /mnt/quiverfs -o allow_other -o ro -o attr_timeout=36000 -o entry_timeout=36000 -o negative_timeout=36000"
        $STORAGE_FUSE_MOUNT /mnt/quiverfs -o allow_other -o ro -o attr_timeout=36000 -o entry_timeout=36000 -o negative_timeout=36000
        if [ $? -ne 0 ]
        then
            echo "Mount failed"
            exit 1
        fi
        echo "Mount done. Testing stat /mnt/quiverfs"
        stat /mnt/quiverfs

        second_mount_enabled=false
        if grep -q 'mount2' /tmp/storageIntercept.config; then
            echo "Mount2 enabled"
            mv /tmp/storageIntercept.config /tmp/storageIntercept.config.bak
            grep -v 'mount1' /tmp/storageIntercept.config.bak > /tmp/storageIntercept.config
            sed -i 's/mountName1/mountName0/g' /tmp/storageIntercept.config

            umount /mnt/quiverfs2
            mkdir -p /mnt/quiverfs2
            echo "$STORAGE_FUSE_MOUNT /mnt/quiverfs2 -o allow_other -o ro -o attr_timeout=36000 -o entry_timeout=36000 -o negative_timeout=36000"
            $STORAGE_FUSE_MOUNT /mnt/quiverfs2 -o allow_other -o ro -o attr_timeout=36000 -o entry_timeout=36000 -o negative_timeout=36000
            if [ $? -ne 0 ]
            then
                echo "Mount2 failed"
                exit 1
            fi

            second_mount_enabled=true
        else
            echo "Mount2 disabled"
        fi
    fi

    runTestCmd="${STORAGE_INTERCEPT_LD_PRELOAD} python3 ${PYTHON_IO_TEST} --folder_path ${_IO_TEST_FOLDER_PATH} --compressed_tar_path ${_IO_TEST_TAR_PATH} --compressed_zip_path ${_IO_TEST_ZIP_PATH}"
    echo "Starting the python io test => ${runTestCmd}"
    eval $runTestCmd
    if [ $? -ne 0 ]
    then
        echo "Python io test failed"
        exit 1
    fi

    echo "Python io test completed"
    echo

    export STORAGE_INTEG_TESTCASE_FILE=$_SAMPLES_DIR
    runTestCmd="${STORAGE_INTERCEPT_LD_PRELOAD} ${EXE_TEST} ${_MOUNT_PATH} ${_NUM_FILES} ${_PARALLELISM}"
    eval $runTestCmd
    if [ $? -ne 0 ]
    then
        echo "Synthetic test failed"
        exit 1
    fi

    unset STORAGE_INTEG_TESTCASE_FILE
    echo "Synthetic test completed"
    echo

    if [ ${_USE_FUSE} = true ]
    then
        echo "umount /mnt/quiverfs"
        umount /mnt/quiverfs
        if [ ${second_mount_enabled} = true ]
        then
            echo "umount /mnt/quiverfs2"
            umount /mnt/quiverfs2
        fi
    fi
}

# ==================================================
# Init script variables
# ==================================================
enable_stacking=false
dataset="small"
mount_path="/mnt/quiverfs"
num_files_to_test=100
parallelism=4
build_configuration=Debug
use_fuse=false
discovery_enabled=false

# ==================================================
# Parse command line args
# ==================================================
while getopts :b:d:n:p:fshe flag
do
    case "${flag}" in
        h) _usage;
            exit 0;;
        b) build_configuration=${OPTARG};;
        d) dataset=${OPTARG};;
        f) use_fuse=true;;
        n) num_files_to_test=${OPTARG};;
        p) parallelism=${OPTARG};;
        s) enable_stacking=true;;
        e) discovery_enabled=true;;
        *) _usage; 
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
elif [ "$dataset" = "largeMultiMountDynamic" ]
then
    mount_path="/mnt/quiverfs/dataset"
    samples_file="${SCRIPT_DIR}/large_dataset_testcases_dynamic_container.txt"
    declare -A io_test_args=(
        [folder_path]="/mnt/quiverfs/dataset/books_wiki_en_corpus"
        [compressed_tar_path]="/mnt/quiverfs/dataset/books_wiki_en_corpus.tar.gz"
        [compressed_zip_path]="/mnt/quiverfs/dataset/books_wiki_en_corpus.zip")
elif [ "$dataset" = "largeDynamicAccount" ]
then
    mount_path="/mnt/quiverfs/storageinterceptionpr1.blob.core.windows.net/dataset"
    samples_file="${SCRIPT_DIR}/large_dataset_testcases_dynamic_account.txt"
    declare -A io_test_args=(
        [folder_path]="/mnt/quiverfs/storageinterceptionpr1.blob.core.windows.net/dataset/books_wiki_en_corpus"
        [compressed_tar_path]="/mnt/quiverfs/storageinterceptionpr1.blob.core.windows.net/dataset/books_wiki_en_corpus.tar.gz"
        [compressed_zip_path]="/mnt/quiverfs/storageinterceptionpr1.blob.core.windows.net/dataset/books_wiki_en_corpus.zip")
else
    samples_file="${SCRIPT_DIR}/large_dataset_testcases.txt"
    declare -A io_test_args=(
        [folder_path]="/mnt/quiverfs/books_wiki_en_corpus"
        [compressed_tar_path]="/mnt/quiverfs/books_wiki_en_corpus.tar.gz"
        [compressed_zip_path]="/mnt/quiverfs/books_wiki_en_corpus.zip")
fi

if [ "$discovery_enabled" = true ]
then
    mkdir -p /serverConfig
    echo -n "1" > /serverConfig/cache_server_count
    echo -n "127.0.{}.1" > /serverConfig/cache_server_endpoint_format
fi

STORAGE_INTERCEPTOR="${REPOSITORY_ROOT}/obj/${build_configuration}/native/linux-x64/publish-storage/storage/libStorageIntercept.so"
LFS_INTERCEPTOR="${REPOSITORY_ROOT}/obj/${build_configuration}/native/linux-x64/publish/libLfsIntercept.so"
PYTHON_IO_TEST="${SCRIPT_DIR}/io_test.py"
EXE_TEST="${REPOSITORY_ROOT}/bin/${build_configuration}/native-storage/storage/StorageInterceptIntegTest"

echo "$(readlink -f ${STORAGE_INTERCEPTOR})"
echo "$(readlink -f ${EXE_TEST})"

# Call the test runner
echo "Starting the integration test"
ioTestArgs="declare -p io_test_args"
_test_runner $enable_stacking "$($ioTestArgs)" $mount_path $samples_file $num_files_to_test $parallelism $use_fuse

# Cleanup after the test
if [ "$discovery_enabled" = true ]
then
    rm -f /serverConfig/cache_server_count /serverConfig/cache_server_endpoint_format
    rmdir /serverConfig
fi

if [ $? -ne 0 ]
then
    echo "Integration test failed"
    exit 1
fi
echo "Integration test completed succesfully"
