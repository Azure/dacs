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

export TERM="xterm"

_log_with_time_()
{
    _MSG_INFO=$1
    echo "[$(date -u)] $_MSG_INFO"
}

_start_pod_() {
    local _yaml_file=${1}

    _log_with_time_ "Starting pod(s). Using yaml file at ${_yaml_file}"
    kubectl --kubeconfig $KUBECONFIG apply -f $_yaml_file
}

_delete_pod_() {
    local _yaml_file=${1}

    _log_with_time_ "Deleting pod(s). Using yaml file at ${_yaml_file}"
    kubectl --kubeconfig $KUBECONFIG delete -f $_yaml_file
}

# This returns the status of each pod that matches the identifier string
_get_pod_status_() {
    local _identifier=${1}
    local _status=$(kubectl --kubeconfig $KUBECONFIG get pods | awk '/'$_identifier'/ {print $3}' | tr \\n ' ')
    echo $_status
}

_wait_for_status_() {
    local _identifier=${1}
    local _desired_status=${2}
    local _timeout=${3}
    local _wait_interval=${4}

    ((t = 0))

    while ((t < _timeout)); do
        local _statuses=$(_get_pod_status_ $_identifier)
        _statuses=($_statuses)
        local _not_matched="false"
        for _status in "${_statuses[@]}"; do
            if [[ "$_status" != "$_desired_status" ]]; then
                _not_matched="true"
                break
            fi
        done

        if [[ "$_not_matched" == "true" ]]; then
            _log_with_time_ "Waiting for Pod(s) ${_identifier} to enter status: ${_desired_status}. ${t} seconds have elapsed..."
            ((t += _wait_interval))
            sleep $_wait_interval
        else
            _log_with_time_ "Pod(s) ${_identifier}* have reached desired status: ${_desired_status}"
            return 0
        fi

    done

    _log_with_time_ "Pod(s) ${_identifier} have failed to reach the desired status: $_desired_status"
    return 1
}

_check_pods_running_() {
    local _identifier=${1}
    if ! _wait_for_status_ $_identifier "Running" "60" "2"; then
        _log_with_time_ "Pod(s) failed to start. Exiting test..."
        return 1
    fi

    return 0
}

_check_pods_completed_() {
    local _identifier=${1}

    if ! _wait_for_status_ $_identifier "Completed" "3600" "30"; then
        # It's possible some jobs take longer than the provided timeout to complete
        # for now exiting here, but TO-DO: can have more checks here
        _log_with_time_ "Some pod(s) have failed. Exiting..."
        return 1
    fi

    _log_with_time_ "All pods have entered status Completed"
}

_start_cacheserver_service_()
{
    if kubectl --kubeconfig $KUBECONFIG get svc | grep -q cacheserver; then
        _log_with_time_ "Cacheserver service already running"
    else
        _log_with_time_ "Starting cacheserver service"
        kubectl --kubeconfig $KUBECONFIG apply -f cache-server-service.yaml
    fi
}

_stop_cacheserver_service_()
{
    if kubectl --kubeconfig $KUBECONFIG get svc | grep -q cacheserver; then
        _log_with_time_ "Stopping cacheserver service"
        kubectl --kubeconfig $KUBECONFIG delete -f cache-server-service.yaml
    else
        _log_with_time_ "cacheserver service is not running"
    fi

}

_setup_server_() {
    local _setup_dir=${1}
    local _configuration_file=${2}

    if [[ -z "${CACHE_SERVER_IMAGE:-}" ]]; then
        _log_with_time_ "CACHE_SERVER_IMAGE must identify the CacheServer image to test"
        return 1
    fi

    local _num_replicas=$(jq '.ServerConfig.Replicas' $_configuration_file)
    local _cache_size=$(jq '.ServerConfig.CacheSize' $_configuration_file)
    local _ephemeral_limit=$(jq '.ServerConfig.EphemeralLimit' $_configuration_file)
    local _server_template="cache-server-app.template.yaml"
    local _server_yaml="${_setup_dir}/cache-server-app-${_cache_size}.yaml"
    cp $_server_template $_server_yaml

    sed -i "s/%NUM_REPLICAS%/$_num_replicas/g" $_server_yaml
    sed -i "s/%CACHE_SIZE%/$_cache_size/g" $_server_yaml
    sed -i "s/%EPHEMERAL_LIMIT%/$_ephemeral_limit/g" $_server_yaml
    sed -i "s|%CACHE_SERVER_IMAGE%|${CACHE_SERVER_IMAGE}|g" $_server_yaml

    echo $_server_yaml
}

_start_server_() {
    local _server_yaml_file=${1}

    # Ensure the cacheserver service is running
    _start_cacheserver_service_

    _start_pod_ $_server_yaml_file

    # Ensure that the cache server(s) have started successfully
    local _server_identifier="cacheserver"
    if ! _check_pods_running_ $_server_identifier; then
        _log_with_time_ "Cache server(s) failed to enter status Running"
        return 1
    fi

    _log_with_time_ "Cache server(s) have entered status Running"
}

_get_client_identifier_() {
    local _client_yaml_file=${1}

    local _client_identifier=$(echo $_client_yaml_file | grep -Eo "stress-client-[a-z|A-Z|0-9]+" | sed -E 's/stress-client-(.*)/\1/g')
    echo $_client_identifier
}

_setup_clients_() {
    local _setup_dir=${1}
    local _configuration_file=${2}
    local _client_template="stress-client.template.yaml"
    local _client_yaml_files=""

    local _num_clients=$(jq '.ClientConfigs | length' $_configuration_file)
    local _account_name=$(jq '.AccountName' $_configuration_file)
    local _account_endpoint=$(jq '.AccountEndpoint' $_configuration_file)
    local _container_name=$(jq '.ContainerName' $_configuration_file)
    local _sas_token=$(jq '.SASToken' $_configuration_file)

    for i in $(seq 0 $((_num_clients - 1))); do
        local _client_name=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].Name' $_configuration_file)
        local _client_type=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].ClientType' $_configuration_file)
        local _num_clients=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].Parallelism' $_configuration_file)
        local _dataset=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].Dataset' $_configuration_file)
        local _expiry=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].KeyExpiry' $_configuration_file)
        local _server_hostnames=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].ServerHostnames' $_configuration_file)
        local _group_id=$(jq --argjson jq_i "$i" '.ClientConfigs[$jq_i].GroupId' $_configuration_file)

        local _client_yaml="${_setup_dir}/stress-client-${_client_name}_${_client_type}_${_num_clients}_${_dataset}_${_expiry}.yaml"
        cp $_client_template $_client_yaml

        sed -i "s/%CLIENTNAME%/$_client_name/g" $_client_yaml
        sed -i "s/%CLIENT_TYPE%/$_client_type/g" $_client_yaml
        sed -i "s/%PARALLELISM%/$_num_clients/g" $_client_yaml
        sed -i "s/%DATASET%/$_dataset/g" $_client_yaml
        sed -i "s/%KEY_EXPIRY%/$_expiry/g" $_client_yaml
        sed -i "s/%SERVER_HOSTNAMES%/$_server_hostnames/g" $_client_yaml
        sed -i "s/%CACHE_GROUP_ID%/$_group_id/g" $_client_yaml

        sed -i "s/%ACCOUNT_NAME%/$_account_name/g" $_client_yaml
        sed -i "s/%ACCOUNT_ENDPOINT%/$_account_endpoint/g" $_client_yaml
        sed -i "s/%CONTAINER_NAME%/$_container_name/g" $_client_yaml
        sed -i "s/%SAS_TOKEN%/$_sas_token/g" $_client_yaml

        _client_yaml_files=$_client_yaml_files" ${_client_yaml}"
    done

    echo $_client_yaml_files
}

_start_client_() {
    local _client_yaml_file=${1}
    _start_pod_ $_client_yaml_file

    # Make sure that the client(s) have started correctly
    local _client_identifier=$(_get_client_identifier_ $_client_yaml_file)
    if ! _check_pods_running_ $_client_identifier; then
        _log_with_time_ "Cache client(s) have failed to enter status Running"
        return 1
    fi

    _log_with_time_ "Cache client(s) have entered status Running"
}

_delete_client_() {
    local _client_yaml_file=${1}
    _delete_pod_ $_client_yaml_file
}

_read_value_from_yaml_file_() {
    local _yaml_file=${1}
    local _key=${2}
    local _value=$(grep -A 1 $_key $_yaml_file | grep -Eo "value: .*" | sed -E 's/value: \"(.*)\"/\1/g')
    echo $_value
}

_get_expected_bytes_to_read_() {
    local _client_yaml_file=${1}
    local _account_name=$(_read_value_from_yaml_file_ $_client_yaml_file "SI_ACCOUNT_NAME")
    local _container_name=$(_read_value_from_yaml_file_ $_client_yaml_file "SI_CONTAINER_NAME")
    local _account_key=$(_read_value_from_yaml_file_ $_client_yaml_file "SI_SAS_TOKEN")
    local _prefix=$(grep -Eo "'--name', '.*'" $_client_yaml_file | cut -d ',' -f2 | tr -d "'" | tr -d " ")
    az storage blob list --container-name "$_container_name" --prefix "$_prefix" \
    --sas-token "$_account_key" \
    --account-name "$_account_name" --query "[*].[properties.contentLength]" \
    --output tsv --num-results 20000 | paste -s -d+ | tr -d $'\r' | bc
}

_verify_io_stats_() {
    local _log_file=${1}
    local _expected_bytes_read=${2}
    local _pod_name=$(echo $_log_file | sed -E 's/(.*)\/(.*)\/(.*)\.log/\3/g')

    _log_with_time_ "Printing IO stats summary for ${_pod_name}"
    grep "Client I/O test results summary" -B 1 -A 2 $_log_file
    local _bytes_read=$(grep -Eo "Bytes read: [0-9]+" $_log_file | sed -E 's/Bytes read: //g')
    if [[ "$_bytes_read" != "$_expected_bytes_read" ]]; then
        return 1
    fi
}

_validate_pod_execution_() {
    local _log_dir=${1}
    local _identifier=${2}
    local _verify_io_stats=${3}
    local _expected_bytes_read=${4}

    # Scan through logs
    local _pod_list=$(kubectl --kubeconfig $KUBECONFIG get pods | awk '/'$_identifier'/ {print $1}' | tr \\n ' ')
    _pod_list=($_pod_list)
    local _io_stats_verified="true"

    for _pod in "${_pod_list[@]}"; do
        kubectl --kubeconfig $KUBECONFIG logs $_pod > ${_log_dir}/${_pod}.log
    done

    for _pod in "${_pod_list[@]}"; do
        if [[ "$_verify_io_stats" == "true" ]]; then
            if ! _verify_io_stats_ "${_log_dir}/${_pod}.log" "${_expected_bytes_read}"; then
                _log_with_time_ "IO stats verification failed for ${_pod}"
                _io_stats_verified="false"
            fi
        fi

        # Ignore IB error(s) and other permissible errors
        allowed_patterns="Couldn't resolve server address"
        if ! grep -E 'DACS_CTR:(ERROR|CRITICAL)' ${_log_dir}/${_pod}.log | grep -Evq "$allowed_patterns"; then
            _log_with_time_ "No DACS_CTR:ERROR or DACS_CTR:CRITICAL logs found in ${_log_dir}/${_pod}.log"
            if [[ "$_io_stats_verified" == "true" ]]; then
                return 0
            else
                return 2
            fi
        else
            _log_with_time_ "Found DACS_CTR:ERROR or DACS_CTR:CRITICAL logs in ${_log_dir}/${_pod}.log"
            if [[ "$_io_stats_verified" == "true" ]]; then
                # Non-critical failure
                _log_with_time_ "Noncritical failure detected"
                return 1
            else
                # Critical failure
                _log_with_time_ "Critical failure detected"
                return 2
            fi
        fi
    done
}

_validate_server_execution_() {
    local _log_dir=${1}
    local _last_ts=${2}

    # Scan through logs
    local _pod_list=$(kubectl --kubeconfig $KUBECONFIG get pods | awk '/cacheserver/ {print $1}' | tr \\n ' ')
    _pod_list=($_pod_list)

    for _pod in "${_pod_list[@]}"; do
        kubectl --kubeconfig $KUBECONFIG logs $_pod --since_time=$_last_ts > ${_log_dir}/${_pod}.log
    done

    for _pod in "${_pod_list[@]}"; do
        # Ignore IB error(s) and other permissible errors
        allowed_patterns="No IB devices found"
        if ! grep -E 'DACS_CTR:(ERROR|CRITICAL)' ${_log_dir}/${_pod}.log | grep -Evq "$allowed_patterns"; then
            _log_with_time_ "No DACS_CTR:ERROR or DACS_CTR:CRITICAL logs found in ${_log_dir}/${_pod}.log"
            return 0
        else
            _log_with_time_ "Found DACS_CTR:ERROR or DACS_CTR:CRITICAL logs in ${_log_dir}/${_pod}.log"
            return 2
        fi

    done
}

_simulate_server_crash_() {
    # Get the list of running cacheserver pods
    pods=$(kubectl --kubeconfig $KUBECONFIG get pods | awk '/cacheserver/ {print $1}' | tr \\n ' ')
    pods=($pods)
    len="${#pods[@]}"
    rand_index=$(shuf -i 0-$((len-1)) -n 1)
    kubectl --kubeconfig $KUBECONFIG delete pod "${pods[$rand_index]}"
}

_cleanup_() {
    local _setup_dir=${1}
    local _yaml_files=$(ls ${_setup_dir})

    for _yaml_file in $_yaml_files; do
        _delete_pod_ "${_setup_dir}/${_yaml_file}"
    done

    _stop_cacheserver_service_
}
