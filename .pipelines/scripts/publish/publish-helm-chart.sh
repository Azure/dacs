#!/bin/bash
# Build and optionally publish Helm charts for cache server
# Uses the same registry/version settings as publish-images.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTPUT_DIR="$REPOSITORY_ROOT/dist"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Registry settings - must be provided by environment variables
REGISTRY="$DOCKER_REGISTRY"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Chart repo
CHART_REPO="oci://$REGISTRY/charts"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function to package and optionally push a chart
# Args: CHART_DIR CHART_NAME CHART_VERSION APP_VERSION
package_chart() {
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    local CHART_VERSION="$3"
    local APP_VERSION="$4"
    
    local CHART_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$CHART_VERSION.tgz"
    local CHART_DESCRIPTION=$(grep '^description:' "$CHART_DIR/Chart.yaml" | cut -d: -f2- | xargs)
    
    # Backup and update Chart.yaml
    cp "$CHART_DIR/Chart.yaml" "$CHART_DIR/Chart.yaml.bak"
    [[ -f "$CHART_DIR/values.yaml" ]] && cp "$CHART_DIR/values.yaml" "$CHART_DIR/values.yaml.bak"
    
    cat > "$CHART_DIR/Chart.yaml" << EOF
apiVersion: v2
name: $CHART_NAME
description: $CHART_DESCRIPTION
type: application
version: $CHART_VERSION
appVersion: "$APP_VERSION"
EOF

    # Update values.yaml with full image repository paths
    if [[ -f "$CHART_DIR/values.yaml" ]]; then
        sed -i "s|^\\(    repository:\\) cache-server|\\1 $REGISTRY/cache-server|" "$CHART_DIR/values.yaml"
        sed -i "s|^\\(      repository:\\) cacheserver-scheduler|\\1 $REGISTRY/cacheserver-scheduler|" "$CHART_DIR/values.yaml"
    fi

    # Package the chart (empty tag in values.yaml defaults to appVersion)
    helm package "$CHART_DIR" -d "$OUTPUT_DIR" --version "$CHART_VERSION" --app-version "$APP_VERSION"
    
    # Restore original files
    mv "$CHART_DIR/Chart.yaml.bak" "$CHART_DIR/Chart.yaml"
    [[ -f "$CHART_DIR/values.yaml.bak" ]] && mv "$CHART_DIR/values.yaml.bak" "$CHART_DIR/values.yaml"
    
    echo "  Built: $CHART_PACKAGE"
    
    # Push if enabled
    if [[ "$PUSH_CHARTS" == "true" ]]; then
        helm push "$CHART_PACKAGE" "$CHART_REPO"
        echo "  Published: $CHART_REPO/$CHART_NAME:$CHART_VERSION"
    fi
}

# Function to build and optionally push a chart
build_and_push_chart() {
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    
    echo ""
    echo "=========================================="
    echo "Building chart: $CHART_NAME"
    echo "=========================================="
    
    # Get base version from existing Chart.yaml (extract only semver part, strip any suffix like -local)
    local RAW_VERSION=$(grep '^version:' "$CHART_DIR/Chart.yaml" | awk '{print $2}' | tr -d '"')
    # Extract just the semver base (e.g., "0.1.0-local" -> "0.1.0")
    local BASE_VERSION=$(echo "$RAW_VERSION" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    [[ -z "$BASE_VERSION" ]] && BASE_VERSION="0.0.0"
    
    # Chart version: always use BASE_VERSION-BUILD_NUMBER format
    local CHART_VERSION="$BASE_VERSION-$BUILD_NUMBER"
    
    echo "Base Version: $BASE_VERSION"
    echo "Chart Version: $CHART_VERSION"
    echo "App Version: $BUILD_NUMBER"
    
    # Build and push versioned chart
    package_chart "$CHART_DIR" "$CHART_NAME" "$CHART_VERSION" "$BUILD_NUMBER"
}

echo "Building Helm charts..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"

# Check if --push flag is provided
PUSH_CHARTS="false"
if [[ "$1" == "--push" ]]; then
    PUSH_CHARTS="true"
fi
    
# Build and push prereq chart
build_and_push_chart "$REPOSITORY_ROOT/helm/dacs-prereq" "dacs-prereq"

# Build and push main cache-server chart
build_and_push_chart "$REPOSITORY_ROOT/helm/cache-server" "cache-server"

# Generate artifact file if pushing
if [[ "$PUSH_CHARTS" == "true" ]]; then
    CACHE_SERVER_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/cache-server/Chart.yaml" | awk '{print $2}' | tr -d '"' | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    PREREQ_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/dacs-prereq/Chart.yaml" | awk '{print $2}' | tr -d '"' | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    [[ -z "$CACHE_SERVER_BASE" ]] && CACHE_SERVER_BASE="0.0.0"
    [[ -z "$PREREQ_BASE" ]] && PREREQ_BASE="0.0.0"
    
    CACHE_SERVER_VERSION="$CACHE_SERVER_BASE-$BUILD_NUMBER"
    PREREQ_VERSION="$PREREQ_BASE-$BUILD_NUMBER"
    
    cat > "$REPOSITORY_ROOT/helm-chart.txt" << EOF
CACHE_SERVER_CHART_VERSION=$CACHE_SERVER_VERSION
CACHE_SERVER_PREREQ_CHART_VERSION=$PREREQ_VERSION
CHART_REPO=$CHART_REPO
EOF
    
    echo ""
    echo "=========================================="
    echo "All charts published successfully!"
    echo "Coordinates saved to helm-chart.txt"
    echo "=========================================="
fi
