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

# Build and optionally publish versioned DACS Helm charts.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTPUT_DIR="${DACS_HELM_OUTPUT_DIR:-$REPOSITORY_ROOT/dist}"
METADATA_FILE="${DACS_HELM_METADATA_FILE:-$REPOSITORY_ROOT/helm-chart.txt}"
VERSION_HELPER="$REPOSITORY_ROOT/.pipelines/scripts/get-version.sh"
SOURCE_VERSION="$("$VERSION_HELPER" source)"
CONTAINER_VERSION="$("$VERSION_HELPER" container)"
CHART_VERSION="$("$VERSION_HELPER" helm)"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Registry settings - used for image coordinates and optional chart pushes.
REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"

# Chart repo
CHART_REPO="oci://$REGISTRY/dacs/helmchart"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if --push flag is provided.
PUSH_CHARTS="false"
if [[ "${1:-}" == "--push" ]]; then
    PUSH_CHARTS="true"
elif [[ "$#" -ne 0 ]]; then
    echo "Usage: $0 [--push]" >&2
    exit 2
fi

# Function to package and optionally push a chart.
# Args: CHART_DIR CHART_NAME
package_chart() (
    local CHART_DIR="$1"
    local CHART_NAME="$2"

    local CHART_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$CHART_VERSION.tgz"
    local CHART_STAGE
    CHART_STAGE="$(mktemp -d "$OUTPUT_DIR/.${CHART_NAME}.XXXXXX")"
    trap 'rm -rf -- "$CHART_STAGE"' EXIT

    cp -a "$CHART_DIR/." "$CHART_STAGE/"

    if [[ "$CHART_NAME" != dacs-prereq ]]; then
        mkdir -p "$CHART_STAGE/charts"
        rm -f "$CHART_STAGE/charts"/dacs-prereq-*.tgz
        cp -a "$REPOSITORY_ROOT/helm/dacs-prereq" "$CHART_STAGE/charts/dacs-prereq"
        sed -i "s|^appVersion:.*|appVersion: \"$CONTAINER_VERSION\"|" \
            "$CHART_STAGE/charts/dacs-prereq/Chart.yaml"
    fi

    # Update values.yaml with the build registry and image versions.
    local VALUES_FILE
    for VALUES_FILE in "$CHART_STAGE/values.yaml" "$CHART_STAGE/charts/dacs-prereq/values.yaml"; do
        [[ -f "$VALUES_FILE" ]] || continue
        sed -i -E \
            "s#^([[:space:]]*registry:)[[:space:]]*dacstest\.azurecr\.io\$#\\1 $REGISTRY#" \
            "$VALUES_FILE"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-server|cache-server)\$#\\1 $REGISTRY/dacs/cache-server#" \
            "$VALUES_FILE"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-scheduler|cacheserver-scheduler)\$#\\1 $REGISTRY/dacs/cacheserver-scheduler#" \
            "$VALUES_FILE"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-controller|cache-controller)\$#\\1 $REGISTRY/dacs/cache-controller#" \
            "$VALUES_FILE"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*cache-setup\$#\\1 $REGISTRY/dacs/cache-setup#" \
            "$VALUES_FILE"
        sed -i -E \
            "s#^([[:space:]]*tag:)[[:space:]]*\"\".*\$#\\1 \"$CONTAINER_VERSION\"#" \
            "$VALUES_FILE"
    done

    # Package the chart (empty tag in values.yaml defaults to appVersion)
    helm package "$CHART_STAGE" -d "$OUTPUT_DIR" \
        --version "$CHART_VERSION" \
        --app-version "$CONTAINER_VERSION"

    echo "  Built: $CHART_PACKAGE"

    # Push if enabled
    if [[ "$PUSH_CHARTS" == "true" ]]; then
        helm push "$CHART_PACKAGE" "$CHART_REPO"
        echo "  Published: $CHART_REPO/$CHART_NAME:$CHART_VERSION"
    fi
)

echo "Building Helm charts..."
echo "Registry: $REGISTRY"
echo "Chart Version: $CHART_VERSION"
echo "App Version: $SOURCE_VERSION"
echo "Container Version: $CONTAINER_VERSION"
    
# Build and push prereq chart
package_chart "$REPOSITORY_ROOT/helm/dacs-prereq" "dacs-prereq"

# Build and push main cache-server chart
package_chart "$REPOSITORY_ROOT/helm/cache-server" "cache-server"

# Build and push dacs chart
package_chart "$REPOSITORY_ROOT/src/subsystems/cache-controller/charts/dacs" "dacs"

cat > "$METADATA_FILE" << EOF
CACHE_SERVER_CHART_VERSION=$CHART_VERSION
CACHE_SERVER_PREREQ_CHART_VERSION=$CHART_VERSION
CACHE_CONTROLLER_CHART_VERSION=$CHART_VERSION
APP_VERSION=$SOURCE_VERSION
CHART_REPO=$CHART_REPO
EOF

echo ""
echo "=========================================="
echo "All charts built successfully!"
echo "Coordinates saved to $METADATA_FILE"
echo "=========================================="
