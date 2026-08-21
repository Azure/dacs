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

# Build and optionally publish Helm charts for cache server
# Uses the same registry/version settings as publish-images.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTPUT_DIR="$REPOSITORY_ROOT/dist"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Registry settings - must be provided by environment variables
REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"
PUBLISH_LATEST="${PUBLISH_LATEST:-false}"

# Chart repo
CHART_REPO="oci://$REGISTRY/charts"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function to package and optionally push a chart
# Args: CHART_DIR CHART_NAME CHART_VERSION APP_VERSION
package_chart() (
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    local CHART_VERSION="$3"
    local APP_VERSION="$4"

    local CHART_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$CHART_VERSION.tgz"
    local CHART_DESCRIPTION
    local CHART_STAGE
    CHART_DESCRIPTION="$(grep '^description:' "$CHART_DIR/Chart.yaml" | cut -d: -f2- | xargs)"
    CHART_STAGE="$(mktemp -d)"
    trap 'rm -rf -- "$CHART_STAGE"' EXIT

    cp -a "$CHART_DIR/." "$CHART_STAGE/"

    cat > "$CHART_STAGE/Chart.yaml" << EOF
apiVersion: v2
name: $CHART_NAME
description: $CHART_DESCRIPTION
type: application
version: $CHART_VERSION
appVersion: "$APP_VERSION"
EOF

    # Update values.yaml with full image repository paths
    if [[ -f "$CHART_STAGE/values.yaml" ]]; then
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-server|cache-server)\$#\\1 $REGISTRY/cache-server#" \
            "$CHART_STAGE/values.yaml"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-scheduler|cacheserver-scheduler)\$#\\1 $REGISTRY/cacheserver-scheduler#" \
            "$CHART_STAGE/values.yaml"
        sed -i -E \
            "s#^([[:space:]]*repository:)[[:space:]]*(acstor/dacs-controller|cache-controller)\$#\\1 $REGISTRY/cache-controller#" \
            "$CHART_STAGE/values.yaml"
        sed -i "s|cache-server:latest|$REGISTRY/cache-server:$APP_VERSION|" "$CHART_STAGE/values.yaml"
    fi

    # Package the chart (empty tag in values.yaml defaults to appVersion)
    helm package "$CHART_STAGE" -d "$OUTPUT_DIR" --version "$CHART_VERSION" --app-version "$APP_VERSION"

    echo "  Built: $CHART_PACKAGE"

    # Push if enabled
    if [[ "$PUSH_CHARTS" == "true" ]]; then
        helm push "$CHART_PACKAGE" "$CHART_REPO"
        echo "  Published: $CHART_REPO/$CHART_NAME:$CHART_VERSION"
    fi
)

# Function to build and optionally push a chart
build_and_push_chart() {
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    
    echo ""
    echo "=========================================="
    echo "Building chart: $CHART_NAME"
    echo "=========================================="
    
    # Get base version from existing Chart.yaml (extract only semver part, strip any suffix like -local)
    local RAW_VERSION
    RAW_VERSION="$(grep '^version:' "$CHART_DIR/Chart.yaml" | awk '{print $2}' | tr -d '"')"
    # Extract just the semver base (e.g., "0.1.0-local" -> "0.1.0")
    local BASE_VERSION
    BASE_VERSION="$(echo "$RAW_VERSION" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')"
    [[ -z "$BASE_VERSION" ]] && BASE_VERSION="0.0.0"
    
    # Chart version: always use BASE_VERSION-BUILD_NUMBER format
    local CHART_VERSION="$BASE_VERSION-$BUILD_NUMBER"
    
    echo "Base Version: $BASE_VERSION"
    echo "Chart Version: $CHART_VERSION"
    echo "App Version: $BUILD_NUMBER"
    
    # Build and push versioned chart
    package_chart "$CHART_DIR" "$CHART_NAME" "$CHART_VERSION" "$BUILD_NUMBER"

    if [[ "$PUBLISH_LATEST" == "true" ]]; then
        package_chart "$CHART_DIR" "$CHART_NAME" "$BASE_VERSION-latest" "latest"
    fi
}

echo "Building Helm charts..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"

# Check if --push flag is provided
PUSH_CHARTS="false"
if [[ "${1:-}" == "--push" ]]; then
    PUSH_CHARTS="true"
fi
    
# Build and push prereq chart
build_and_push_chart "$REPOSITORY_ROOT/helm/dacs-prereq" "dacs-prereq"

# Build and push main cache-server chart
build_and_push_chart "$REPOSITORY_ROOT/helm/cache-server" "cache-server"

# Build and push dacs chart
build_and_push_chart "$REPOSITORY_ROOT/src/subsystems/cache-controller/charts/dacs" "dacs"

# Generate artifact file if pushing
if [[ "$PUSH_CHARTS" == "true" ]]; then
    CACHE_SERVER_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/cache-server/Chart.yaml" | awk '{print $2}' | tr -d '"' | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    PREREQ_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/dacs-prereq/Chart.yaml" | awk '{print $2}' | tr -d '"' | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    CONTROLLER_BASE=$(grep '^version:' "$REPOSITORY_ROOT/src/subsystems/cache-controller/charts/dacs/Chart.yaml" | awk '{print $2}' | tr -d '"' | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    [[ -z "$CACHE_SERVER_BASE" ]] && CACHE_SERVER_BASE="0.0.0"
    [[ -z "$PREREQ_BASE" ]] && PREREQ_BASE="0.0.0"
    [[ -z "$CONTROLLER_BASE" ]] && CONTROLLER_BASE="0.0.0"
    
    CACHE_SERVER_VERSION="$CACHE_SERVER_BASE-$BUILD_NUMBER"
    PREREQ_VERSION="$PREREQ_BASE-$BUILD_NUMBER"
    CONTROLLER_VERSION="$CONTROLLER_BASE-$BUILD_NUMBER"
    
    cat > "$REPOSITORY_ROOT/helm-chart.txt" << EOF
CACHE_SERVER_CHART_VERSION=$CACHE_SERVER_VERSION
CACHE_SERVER_PREREQ_CHART_VERSION=$PREREQ_VERSION
CACHE_CONTROLLER_CHART_VERSION=$CONTROLLER_VERSION
CHART_REPO=$CHART_REPO
EOF

    if [[ "$PUBLISH_LATEST" == "true" ]]; then
        cat >> "$REPOSITORY_ROOT/helm-chart.txt" << EOF
CACHE_SERVER_LATEST_CHART_VERSION=$CACHE_SERVER_BASE-latest
CACHE_SERVER_PREREQ_LATEST_CHART_VERSION=$PREREQ_BASE-latest
CACHE_CONTROLLER_LATEST_CHART_VERSION=$CONTROLLER_BASE-latest
EOF
    fi
    
    echo ""
    echo "=========================================="
    echo "All charts published successfully!"
    echo "Coordinates saved to helm-chart.txt"
    echo "=========================================="
fi
