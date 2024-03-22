# This sub-script is used to test the eviction integration with the cache server.
# Can not run independently, should be sourced by `storage_intercept/integration_tests/pipeline_test.sh`
# Fundamentally these tests depend on timings between client making requests and server threads running eviction passes/snapshots
# they should be stable, but if they fail, try adjusting the sleep times in the tests
########################################################################################################################
TEST_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/linux-x64/publish-storage/storage/cacheServerTestClient"
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
# Run the cache client lib tests against the cache server with eviction and snapshotting enabled
function __run_cache_client_lib_test_with_eviction__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"
    export CACHESERVER_EVICTION_ENABLED="true"
    export CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD="5s"
    export CACHESERVER_EVICTION_PASS_FREQUENCY="1s"
    export CACHESERVER_EVICTION_SIZE_THRESHOLD="0.00000005"

    stop_cache_server
    start_cache_server
    __logsleep__ 10

    echo "Running cache client lib test with eviction enabled"
    pushd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
    eval "ctest -V --no-tests=error -j -1 -R CacheClientLibTests"
    if [ $? -ne 0 ]
    then
        echo "cache client lib unit test failed"
        stop_cache_server
        exit 1
    fi
    echo ""
    popd

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
# Test to add files and continue processing incoming requests while eviction thread runs
function __run_eviction_test_with_parallel_upload_download__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"
    export CACHESERVER_EVICTION_ENABLED="true"
    export CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD="10s"
    export CACHESERVER_EVICTION_PASS_FREQUENCY="2s"
    export CACHESERVER_EVICTION_SIZE_THRESHOLD="0.00000005"

    stop_cache_server
    start_cache_server
    __logsleep__ 10

    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    # Upload some files to start with
    $TEST_CLIENT_EXE uploadfile file0 group1
    $TEST_CLIENT_EXE uploadfile file1 group1

    # Keep uploading/downloading in a loop until eviction is (expected to be) done
    runtime="12 second"
    endtime=$(date -ud "$runtime" +%s) 

    ((i = 0))
    while [[ $(date -u +%s) -le $endtime ]]; do
        eval "$TEST_CLIENT_EXE uploaddownloadmany 1000 30"
        if [ $? -ne 0 ]; then
            echo "Expected uploads, downloads to succeed"
            stop_cache_server
            exit 1
        else
            ((i += 1))
            echo "Uploads, downloads succeeded ($i)"
        fi
    done

    # Now at this point the items that were first uploaded and not accessed
    # should have been evicted
    eval "$TEST_CLIENT_EXE downloadfile file0"
    if [ $? -eq 0 ]; then
        echo "Expected file0 to have been deleted"
        stop_cache_server
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile file1"
    if [ $? -eq 0 ]; then
        echo "Expected file1 to have been deleted"
        stop_cache_server
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
# Test to add files and restore from snapshot with stale items deleted
function __run_eviction_test_with_restart_snapshot__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"
    export CACHESERVER_EVICTION_ENABLED="true"
    export CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD="10s"
    export CACHESERVER_EVICTION_PASS_FREQUENCY="2s"
    export CACHESERVER_EVICTION_SIZE_THRESHOLD="0.00000005"

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

    __logsleep__ 5 # Wait out the rest of the eviction window
    stop_cache_server keep_cache
    start_cache_server
    # Wait for cache server to start, don't wait too long or file2 will expire
    __logsleep__ 2
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    eval "$TEST_CLIENT_EXE downloadfile file0"
    if [ $? -eq 0 ]; then
        stop_cache_server
        echo "Expected file0 to have been deleted"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile file1"
    if [ $? -eq 0 ]; then
        stop_cache_server
        echo "Expected file1 to have been deleted"
        exit 1
    fi
    
    eval "$TEST_CLIENT_EXE downloadfile file2"
    if [ $? -ne 0 ]; then
        stop_cache_server
        echo "Expected file2 to be available"
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################