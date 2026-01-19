# This sub-script is used to test the snapshot integration with the cache server.
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
# Tests to add and delete files, take a snapshot, restart server and validate state.
function __run_restart_snapshot_basic_test__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"

    stop_cache_server
    start_cache_server
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    $TEST_CLIENT_EXE uploadfile file0 group1
    $TEST_CLIENT_EXE uploadfile file1 group1
    $TEST_CLIENT_EXE uploadfile file2 group1
    $TEST_CLIENT_EXE uploadfile blob1 group2
    $TEST_CLIENT_EXE uploadfile blob2 group2
    $TEST_CLIENT_EXE deletegroup group1

    __logsleep__ 10 # Wait for snapshot
    stop_cache_server keep_cache
    start_cache_server
    __logsleep__ 10 # Wait for cache server to start
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    eval "$TEST_CLIENT_EXE downloadfile_expect_notfound file0"
    if [ $? -ne 0 ]; then
        echo "Expected file0 to have been deleted (NOT_FOUND)"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile_expect_notfound file1"
    if [ $? -ne 0 ]; then
        echo "Expected file1 to have been deleted (NOT_FOUND)"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile_expect_notfound file2"
    if [ $? -ne 0 ]; then
        echo "Expected file2 to have been deleted (NOT_FOUND)"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile blob1"
    if [ $? -ne 0 ]; then
        echo "Expected blob1 to exist"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile blob2"
    if [ $? -ne 0 ]; then
        echo "Expected blob2 to exist"
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
# Test to ensure server handles 1) extra files in disk not in snapshot 2) extra files in snapshot not in disk
function __test_snapshot_disk_mismatch__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"
    SERVER_LOG_FILE="/tmp/temp_cache_server.log"

    start_cache_server
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    $TEST_CLIENT_EXE uploadfile file0 group1
    $TEST_CLIENT_EXE uploadfile file1 group2
    $TEST_CLIENT_EXE uploadfile file3 group1
    $TEST_CLIENT_EXE uploadfile file4 group2
    $TEST_CLIENT_EXE uploadfile file5 group1
    $TEST_CLIENT_EXE uploadfile file6 group2
    $TEST_CLIENT_EXE uploadfile file7 group1
    $TEST_CLIENT_EXE uploadfile file8 group2

    __logsleep__ 10 # Wait for snapshot
    ps -e
    stop_cache_server keep_cache
    ps -e
    # Delete alternate objects from the disk cache
    counter="0"
    find "$CACHE_SERVER_PATH/_cache" -type f -print0 | while IFS= read -r -d '' file; do
        counter=$((counter+1))
        should_delete=$((counter%2))
        if [ "$should_delete" -eq 0 ]; then
            rm -v "$file"
        fi
    done

    start_cache_server $SERVER_LOG_FILE
    __logsleep__ 10
    stop_cache_server

    if ! grep -q "File in snapshot not found on disk" $SERVER_LOG_FILE; then
        echo "Expected server to detect that some files in snapshot are not found on disk"
        exit 1
    fi

    echo "${FUNCNAME[0]} passed"

}
########################################################################################################################
# Test to ensure server does not crash on reading corrupted snapshot, and is able to process requests.
function __test_no_crash_on_invalid_snapshot__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"

    # Create corruped snapshot
    stop_cache_server
    mkdir -p "$CACHE_SERVER_PATH/snapshot"
    echo "this is a random string" > "$CACHE_SERVER_PATH/snapshot/current.snapshot"
    start_cache_server
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    # Check server is still responsive
    eval "$TEST_CLIENT_EXE uploadfile file0 group0"
    if [ $? -ne 0 ]; then
        echo "Expected upload to succeed"
        exit 1
    fi

    eval "$TEST_CLIENT_EXE downloadfile file0"
    if [ $? -ne 0 ]; then
        echo "Expected download to succeed"
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
# Test to ensure server does not crash on reading truncated snapshot, and is able to process requests.
function __test_no_crash_on_truncated_snapshot__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"

    # Create corruped snapshot
    stop_cache_server
    start_cache_server
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="5"
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

    # Populate server
    eval "$TEST_CLIENT_EXE uploadmany group0 100 5"
    if [ $? -ne 0 ]; then
        echo "Expected upload to succeed"
        exit 1
    fi

    # Wait for snapshot
    __logsleep__ 6
    stop_cache_server keep_cache

    # Truncate snapshot
    snapshot_size=$(stat -c "%s" "$CACHE_SERVER_PATH/snapshot/current.snapshot")
    half_size=$((snapshot_size/2))
    truncate -s -$half_size "$CACHE_SERVER_PATH/snapshot/current.snapshot"

    start_cache_server
    __logsleep__ 10

    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
    eval "$TEST_CLIENT_EXE uploaddownloadmany 100 5"
    if [ $? -ne 0 ]; then
        echo "Expected uploads, downloads to succeed"
        exit 1
    fi

    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
# Test to ensure upload, download, and snapshot can be done in parallel
function __test_upload_download_snapshot_in_parallel__()
{
    echo "Running ${FUNCNAME[0]}"
    export CACHESERVER_HOSTNAME="testhost"
    export CACHESERVER_SNAPSHOT_INTERVAL="1"
    SERVER_LOG_FILE="/tmp/temp_cache_server.log"

    stop_cache_server
    start_cache_server "$SERVER_LOG_FILE"
    __logsleep__ 10
    export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
    snapshot_file="$CACHE_SERVER_PATH/snapshot/current.snapshot"
    snapshot_temp_file="$CACHE_SERVER_PATH/snapshot/temp.snapshot"

    rm -f "$snapshot_file"
    rm -f "$snapshot_temp_file"

    # Keep uploading/downloading in a loop until snapshot is (expected to be) created
    loop_for_seconds=3
    start_time=$(date +%s)
    while true; do
        current_time=$(date +%s)
        time_difference=$((current_time - start_time))

        if ((time_difference >= loop_for_seconds)); then
            break
        fi

        eval "$TEST_CLIENT_EXE uploaddownloadmany 1000 30"
        if [ $? -ne 0 ]; then
            echo "Expected uploads, downloads to succeed"
            exit 1
        else
            echo "Uploads, downloads succeeded ($i)"
        fi
    done

    if [[ ! -e "$snapshot_file" ]]; then
        echo "Expected snapshot file to exist"
        exit 1
    fi
    
    stop_cache_server
    echo "${FUNCNAME[0]} passed"
}
########################################################################################################################
