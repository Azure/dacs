#!/bin/bash
# Build and optionally publish Helm chart for cache server
# Uses the same registry/version settings as publish-images.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
CHART_DIR="$REPOSITORY_ROOT/helm/cache-server"
OUTPUT_DIR="$REPOSITORY_ROOT/dist"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

REGISTRY="${DOCKER_REGISTRY:-dacs.azurecr.io}"
REGISTRY_NAMESPACE="${DOCKER_REGISTRY_NAMESPACE:-dacs/images}"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Image tag matches what publish-images.sh creates
IMAGE_TAG="$BUILD_NUMBER-linux-amd64"

# Get base version from existing Chart.yaml
BASE_VERSION=$(grep '^version:' "$CHART_DIR/Chart.yaml" | awk '{print $2}' | tr -d '"')
if [[ -z "$BASE_VERSION" ]]; then
    BASE_VERSION="0.0.0"
fi

# Chart version: if BUILD_NUMBER is already semver, use it; otherwise append to base version
if [[ "$BUILD_NUMBER" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]]; then
    CHART_VERSION="$BUILD_NUMBER"
else
    CHART_VERSION="$BASE_VERSION-$BUILD_NUMBER"
fi

# Chart settings
CHART_NAME="cache-server"
CHART_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$CHART_VERSION.tgz"
CHART_REPO="oci://$REGISTRY/dacs/charts"

echo "Building Helm chart..."
echo "Registry: $REGISTRY"
echo "Registry Namespace: $REGISTRY_NAMESPACE"
echo "Base Version: $BASE_VERSION"
echo "Chart Version: $CHART_VERSION"
echo "App Version: $BUILD_NUMBER"
echo "Image Tag: $IMAGE_TAG"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Backup original files
cp "$CHART_DIR/Chart.yaml" "$CHART_DIR/Chart.yaml.bak"
cp "$CHART_DIR/values.yaml" "$CHART_DIR/values.yaml.bak"

cleanup() {
    mv "$CHART_DIR/Chart.yaml.bak" "$CHART_DIR/Chart.yaml" 2>/dev/null || true
    mv "$CHART_DIR/values.yaml.bak" "$CHART_DIR/values.yaml" 2>/dev/null || true
}
trap cleanup EXIT

# Update Chart.yaml
cat > "$CHART_DIR/Chart.yaml" << EOF
apiVersion: v2
name: $CHART_NAME
description: Distributed Cache for K8s
type: application
version: $CHART_VERSION
appVersion: $BUILD_NUMBER
EOF

# Update values.yaml with full image repository
sed -i "s|^\\(    repository:\\) cache-server|\\1 $REGISTRY/$REGISTRY_NAMESPACE/cache-server|" "$CHART_DIR/values.yaml"
sed -i "s|^\\(      repository:\\) cacheserver-scheduler|\\1 $REGISTRY/$REGISTRY_NAMESPACE/cacheserver-scheduler|" "$CHART_DIR/values.yaml"

# Package the chart
helm package "$CHART_DIR" -d "$OUTPUT_DIR" --version "$CHART_VERSION" --app-version "$BUILD_NUMBER"

echo ""
echo "Chart built: $CHART_PACKAGE"

# Push if --push flag is provided
if [[ "$1" == "--push" ]]; then
    echo ""
    echo "Publishing chart to $CHART_REPO..."
    
    # Login to ACR if it's an Azure registry
    if echo "$REGISTRY" | grep -q "azurecr.io"; then
        echo "Logging in to Azure Container Registry..."
        REGISTRY_NAME="${REGISTRY%%.*}"
        TOKEN=$(az acr login --name "$REGISTRY_NAME" --expose-token --output tsv --query accessToken)
        echo "$TOKEN" | helm registry login "$REGISTRY" -u 00000000-0000-0000-0000-000000000000 --password-stdin
    fi
    
    # Push the versioned chart
    helm push "$CHART_PACKAGE" "$CHART_REPO"
    
    # Also push as latest (appVersion will be "latest" so image tag defaults to "latest")
    LATEST_CHART_VERSION="$BASE_VERSION-latest"
    LATEST_PACKAGE="$OUTPUT_DIR/$CHART_NAME-$LATEST_CHART_VERSION.tgz"
    helm package "$CHART_DIR" -d "$OUTPUT_DIR" --version "$LATEST_CHART_VERSION" --app-version "latest"
    helm push "$LATEST_PACKAGE" "$CHART_REPO"
    
    # Generate artifact file
    cat > "$REPOSITORY_ROOT/helm-chart.txt" << EOF
CHART_NAME=$CHART_NAME
CHART_VERSION=$CHART_VERSION
CHART_REPO=$CHART_REPO/$CHART_NAME:$CHART_VERSION
CHART_REPO_LATEST=$CHART_REPO/$CHART_NAME:latest
EOF
    
    echo ""
    echo "Chart published: $CHART_REPO/$CHART_NAME:$CHART_VERSION"
    echo "Chart published: $CHART_REPO/$CHART_NAME:$LATEST_CHART_VERSION"
    echo "Coordinates saved to helm-chart.txt"
fi
