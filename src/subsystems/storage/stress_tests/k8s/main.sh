#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# Import all test utility functions
source utils.sh

# Setup job output directory
[[ -z $1 ]] && echo "Output directory is required..." && exit 1
output_dir=$1
setup_dir=${output_dir}/setup
logs_dir=${output_dir}/logs

# Test scenario configuration file
[[ -z $2 ]] && echo "Path to test scenario configuration must be provided..." && exit 1
configuration_file=$2

test_name=$(jq '.TestName' $configuration_file)
_log_with_time_ "Starting test scenario - ${test_name}"

current_ts=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
# Start cacheserver(s) - this can potentially be moved out in the future
server_yaml_file=$(_setup_server_ "${setup_dir}" "${configuration_file}")
if ! _start_server_ "${server_yaml_file}"; then
    exit 1
fi

# Additional test parameters
crash_interval=$(jq '.ServerConfig.CrashInterval' $configuration_file)
sequential_test=$(jq '.SequentialTest' $configuration_file)
sleep_interval=$(jq '.SleepInterval' $configuration_file)

# Generate client yaml files
client_yaml_files=$(_setup_clients_ "${setup_dir}" "${configuration_file}")

# Get expected bytes to be read by each client
expected_bytes_read=()
for client_yaml_file in $client_yaml_files; do
    _bytes=$(_get_expected_bytes_to_read_ "$client_yaml_file")
    expected_bytes_read+=( "$_bytes" )
    client_identifier=$(_get_client_identifier_ $client_yaml_file)
    _log_with_time_ "${client_identifier} expected bytes: ${_bytes}"
done

# Client loop, run forever until exit
((count = 1))
((successful_runs = 0))
((noncritical_failures = 0))
((critical_failures = 0))
samples=()

while true; do
    # Start cache client
    for client_yaml_file in $client_yaml_files; do
        if ! _start_client_ "${client_yaml_file}"; then
            exit 1
        fi

        # If sequential test, wait for client to complete before
        # spawning next client
        if [[ "$sequential_test" == "true" ]]; then
            _log_with_time_ "Sequential test is true. Waiting for cache client(s) to complete before spawning next client..."
            client_identifier=$(_get_client_identifier_ $client_yaml_file)
            if ! _check_pods_completed_ "$client_identifier"; then
                exit 1
            fi

            _log_with_time_ "Cache client(s): ${client_identifier} have completed."
        fi
    done

    if [[ "$sequential_test" != "true" ]]; then
        # Induce a server crash if config is enabled
        if [[ "$crash_interval" != "0" ]] && \
            [[ "$((count % crash_interval))" == "0" ]]; then
             sleep 7
             _simulate_server_crash_
        fi

        # Wait for all clients to complete
        _log_with_time_ "Waiting for all cache client(s) to complete..."
        ((_i = 0))
        for client_yaml_file in $client_yaml_files; do
            client_identifier=$(_get_client_identifier_ $client_yaml_file)
            io_verification=$(jq --argjson jq_i "$_i" '.ClientConfigs[$jq_i].IOVerification' $configuration_file)
            if ! _check_pods_completed_ "$client_identifier"; then
                exit 1
            fi

            _log_with_time_ "Cache client(s): ${client_identifier} have completed."
            ((_i += 1))
        done
    fi

    _log_with_time_ "Validating all clients executed successfully"
    log_dir=${logs_dir}/$count
    mkdir -p $log_dir

    ((_i = 0))
    success="true"
    critical_failure="false"
    for client_yaml_file in $client_yaml_files; do

        client_identifier=$(_get_client_identifier_ $client_yaml_file)
        _log_with_time_ "Validating Cache client(s): ${client_identifier}."
        io_verification=$(jq --argjson jq_i "$_i" '.ClientConfigs[$jq_i].IOVerification' $configuration_file)
        _validate_pod_execution_ "${log_dir}" "${client_identifier}" "${io_verification}" "${expected_bytes_read[$_i]}"

        case "$?" in
            "0")
                _log_with_time_ "Client(s): ${client_identifier} have executed successfully"
                ;;
            "1")
                success="false"
                ;;
            "2")
                success="false"
                critical_failure="true"
                ;;
        esac

        ((_i += 1))
    done

    # Check server logs at this point as well
    _validate_server_execution_ "${log_dir}" "${current_ts}"
    case "$?" in
        "0")
            _log_with_time_ "No errors in cache server(s)"
            ;;
        "1")
            success="false"
            ;;
        "2")
            success="false"
            critical_failure="true"
            ;;
    esac

    if [[ "$success" == "true" ]]; then
        _log_with_time_ "All pods have executed successfully."
        ((successful_runs += 1))
    elif [[ "$critical_failure" == "false" ]]; then
        _log_with_time_ "Some pods have encountered non-critical failures."
        ((noncritical_failures += 1))
    else
        _log_with_time_ "Some pods have encountered critical failures."
        ((critical_failures += 1))
    fi

    # Delete clients for next iteration
    for client_yaml_file in $client_yaml_files; do
        _delete_client_ $client_yaml_file
    done

    echo "###==============================================================================================================================================================###"
    _log_with_time_ "Finished $count runs. SuccessfulRuns=$successful_runs NonCriticalFailures=$noncritical_failures CriticalFailures=$critical_failures"
    echo "###==============================================================================================================================================================###"

    ((count += 1))
    if [[ "$sleep_interval" != "0" ]]; then
        _log_with_time_ "Sleeping for $sleep_interval seconds before next iteration..."
        sleep $sleep_interval
    fi

    current_ts=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
done

# Cleanup happens in launcher script
