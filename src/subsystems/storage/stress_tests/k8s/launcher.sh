#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

[[ -z $1 ]] && echo "Correct usage: ./launcher.sh <path_to_kubeconfig> <path_to_test_scenario>" && exit 1
[[ -z $2 ]] && echo "Correct usage: ./launcher.sh <path_to_kubeconfig> <path_to_test_scenario>" && exit 1

ts=$(date +%s)
output_dir="exp_${ts}"
setup_dir=${output_dir}/setup
logs_dir=${output_dir}/logs

mkdir -p $setup_dir
mkdir -p $logs_dir
export KUBECONFIG="$1"

_exit_handler_() {
    echo -e "\nExiting test..."
    mkdir -p "$logs_dir/exit"
    pods=$(kubectl --kubeconfig $KUBECONFIG get pods | awk '/cacheserver/{print $1} /stress-client/{print $1}' | tr \\n ' ')
    pods=($pods)

    for pod in "${pods[@]}"; do
        kubectl --kubeconfig $KUBECONFIG logs $pod > "$logs_dir/exit/$pod.log"
    done

    yaml_files=$(ls $setup_dir)
    for file in $yaml_files; do
        kubectl --kubeconfig $KUBECONFIG delete -f "$setup_dir/$file"
    done

    kubectl --kubeconfig $KUBECONFIG delete -f cache-server-service.yaml
    exit 0
}
trap _exit_handler_ SIGINT

./main.sh $output_dir $2 | tee -a $logs_dir/test.log
_exit_handler_
