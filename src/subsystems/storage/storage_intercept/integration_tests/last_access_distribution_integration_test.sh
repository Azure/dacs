# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# This sub-script is used to test the get distribution and delete apis based on last access time.
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
# Can not run independently, should be sourced by `storage_intercept/integration_tests/pipeline_test.sh`
########################################################################################################################
TEST_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cacheServerTestClient"
########################################################################################################################
# Log every second while sleeping
function __logsleep__()
{
    seconds=$1
    echo "Sleeping for $seconds s"
    for i in $(seq 0 $1)
    do
        sleep 1
        remaining=$((seconds-i))
        echo "Sleeping for $remaining s..."
    done

}
########################################################################################################################
# Test to check last access time distribution of cache items
function __run__test__last_access_time_distribution__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"

    stop_cache_server
    start_cache_server
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    $TEST_CLIENT_EXE uploadfile file0 group1
    $TEST_CLIENT_EXE uploadfile file1 group1
    $TEST_CLIENT_EXE uploadfile file2 group1

    __logsleep__ 8
    # Touch file2 again
    eval "$TEST_CLIENT_EXE downloadfile file2"
    if [ $? -ne 0 ]; then
        echo "Expected file2 to be available"
        stop_cache_server
        exit 1
    fi

    eval "$TEST_CLIENT_EXE getaccesstimedistribution 1"
    if [ $? -ne 0 ]; then
        stop_cache_server
        echo "Expected getaccesstimedistribution to succeed"
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}