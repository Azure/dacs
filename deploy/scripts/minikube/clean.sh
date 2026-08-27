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

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load configuration from .pipelines location (shared config)
CONFIG_FILE="$REPOSITORY_ROOT/.pipelines/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Use /mnt for minikube if available
if [ -d "/mnt/minikube" ]; then
    export MINIKUBE_HOME="/mnt/minikube"
fi

CLUSTER_NAME="${CLUSTER_NAME:-minikube}"

echo "Deleting Minikube cluster '$CLUSTER_NAME'..."
if ! minikube delete --profile "$CLUSTER_NAME"; then
    echo "Warning: Minikube profile '$CLUSTER_NAME' could not be deleted; continuing with filesystem cleanup."
fi

# Clean up Minikube and kubelet data on /mnt even when cluster deletion failed.
if [ -d "/mnt/minikube" ]; then
    echo "Cleaning up /mnt/minikube..."
    sudo rm -rf /mnt/minikube
fi

if [ -d "/mnt/kubelet" ]; then
    echo "Cleaning up /mnt/kubelet..."
    sudo rm -rf /mnt/kubelet/*
fi

echo "Minikube cleanup completed."
