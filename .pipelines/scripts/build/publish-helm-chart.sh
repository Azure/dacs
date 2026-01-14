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

REGISTRY="${DOCKER_REGISTRY:-dacs.azurecr.io}"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Chart repo
CHART_REPO="oci://$REGISTRY/charts"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Function to package and optionally push a chart
# Args: CHART_DIR CHART_NAME HAS_IMAGES CHART_VERSION APP_VERSION IMAGE_TAG
package_chart() {
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    local HAS_IMAGES="$3"
    local CHART_VERSION="$4"
    local APP_VERSION="$5"
    local IMAGE_TAG="$6"
    
    local CHART_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$CHART_VERSION.tgz"
    local CHART_DESCRIPTION=$(grep '^description:' "$CHART_DIR/Chart.yaml" | cut -d: -f2- | xargs)
    
    # Backup original files
    cp "$CHART_DIR/Chart.yaml" "$CHART_DIR/Chart.yaml.bak"
    [[ -f "$CHART_DIR/values.yaml" ]] && cp "$CHART_DIR/values.yaml" "$CHART_DIR/values.yaml.bak"
    
    # Update Chart.yaml
    cat > "$CHART_DIR/Chart.yaml" << EOF
apiVersion: v2
name: $CHART_NAME
description: $CHART_DESCRIPTION
type: application
version: $CHART_VERSION
appVersion: "$APP_VERSION"
EOF

    # Update values.yaml with full image repository and tag (only for charts with images)
    if [[ "$HAS_IMAGES" == "true" && -f "$CHART_DIR/values.yaml" ]]; then
        # Update image repositories
        sed -i "s|^\\(    repository:\\) cache-server|\\1 $REGISTRY/cache-server|" "$CHART_DIR/values.yaml"
        sed -i "s|^\\(      repository:\\) cacheserver-scheduler|\\1 $REGISTRY/cacheserver-scheduler|" "$CHART_DIR/values.yaml"

        # Update image tags (replace empty tags with the actual tag)
        sed -i "s|^\\(    tag:\\) \"\"|\\1 \"$IMAGE_TAG\"|" "$CHART_DIR/values.yaml"
        sed -i "s|^\\(      tag:\\) \"\"|\\1 \"$IMAGE_TAG\"|" "$CHART_DIR/values.yaml"
    fi
    
    # Package the chart
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

# Function to build and optionally push a chart (versioned + latest)
build_and_push_chart() {
    local CHART_DIR="$1"
    local CHART_NAME="$2"
    local HAS_IMAGES="$3"
    
    echo ""
    echo "=========================================="
    echo "Building chart: $CHART_NAME"
    echo "=========================================="
    
    # Get base version from existing Chart.yaml (extract only semver part, strip any suffix like -local)
    local RAW_VERSION=$(grep '^version:' "$CHART_DIR/Chart.yaml" | awk '{print $2}' | tr -d '"')
    # Extract just the semver base (e.g., "0.1.0-local" -> "0.1.0")
    local BASE_VERSION=$(echo "$RAW_VERSION" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')
    [[ -z "$BASE_VERSION" ]] && BASE_VERSION="0.0.0"
    
    # Chart version: if BUILD_NUMBER is already semver, use it; otherwise append to base version
    local CHART_VERSION
    if [[ "$BUILD_NUMBER" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; then
        CHART_VERSION="$BUILD_NUMBER"
    else
        CHART_VERSION="$BASE_VERSION-$BUILD_NUMBER"
    fi
    
    # Image tag matches what publish-images.sh creates
    local IMAGE_TAG="$BUILD_NUMBER-linux-amd64"
    
    echo "Base Version: $BASE_VERSION"
    echo "Chart Version: $CHART_VERSION"
    echo "App Version: $BUILD_NUMBER"
    echo "Image Tag: $IMAGE_TAG"
    
    # Build and push versioned chart
    package_chart "$CHART_DIR" "$CHART_NAME" "$HAS_IMAGES" "$CHART_VERSION" "$BUILD_NUMBER" "$IMAGE_TAG"
    
    # Build and push latest chart (only if pushing and not already "latest")
    if [[ "$PUSH_CHARTS" == "true" && "$BUILD_NUMBER" != "latest" ]]; then
        local LATEST_VERSION="$BASE_VERSION-latest"
        package_chart "$CHART_DIR" "$CHART_NAME" "$HAS_IMAGES" "$LATEST_VERSION" "latest" "latest-linux-amd64"
    fi
}

echo "Building Helm charts..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"

# Check if --push flag is provided
PUSH_CHARTS="false"
if [[ "$1" == "--push" ]]; then
    PUSH_CHARTS="true"
    
    # Login to ACR if it's an Azure registry
    if [[ "$REGISTRY" == *".azurecr.io" ]]; then
        echo ""
        echo "Logging in to Azure Container Registry..."
        REGISTRY_NAME="${REGISTRY%.azurecr.io}"
        TOKEN=$(az acr login --name "$REGISTRY_NAME" --expose-token --output tsv --query accessToken)
        echo "$TOKEN" | helm registry login "$REGISTRY" -u 00000000-0000-0000-0000-000000000000 --password-stdin
    fi
fi

# Build and push prereq chart (no container images)
build_and_push_chart "$REPOSITORY_ROOT/helm/dacs-prereq" "dacs-prereq" "false"

# Build and push main cache-server chart (has container images)
build_and_push_chart "$REPOSITORY_ROOT/helm/cache-server" "cache-server" "true"

# Generate artifact file if pushing
if [[ "$PUSH_CHARTS" == "true" ]]; then
    CACHE_SERVER_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/cache-server/Chart.yaml" | awk '{print $2}' | tr -d '"')
    PREREQ_BASE=$(grep '^version:' "$REPOSITORY_ROOT/helm/dacs-prereq/Chart.yaml" | awk '{print $2}' | tr -d '"')
    
    if [[ "$BUILD_NUMBER" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; then
        CACHE_SERVER_VERSION="$BUILD_NUMBER"
        PREREQ_VERSION="$BUILD_NUMBER"
    else
        CACHE_SERVER_VERSION="$CACHE_SERVER_BASE-$BUILD_NUMBER"
        PREREQ_VERSION="$PREREQ_BASE-$BUILD_NUMBER"
    fi
    
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
