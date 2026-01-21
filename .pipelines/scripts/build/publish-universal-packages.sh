#!/bin/bash
# publish-universal-packages.sh
# Publishes universal packages to Azure Artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# =============================================================================
# Configuration
# =============================================================================
AZ_ORGANIZATION="internal-reference-removed"
AZ_PROJECT="internal"
AZ_ARTIFACT_FEED="Storage-Cache-Feed"

PACKAGE_BASE_NAME="dacs"
BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Release}"
ARCHITECTURES=("amd64" "arm64")

# Version is inherited from parent script (publish-packages.sh) via environment
if [ -z "$UNIVERSAL_PACKAGE_VERSION" ]; then
  echo "Error: UNIVERSAL_PACKAGE_VERSION not set"
  exit 1
fi

if [ -z "$PACKAGE_CHANNEL" ]; then
  echo "Error: PACKAGE_CHANNEL not set"
  exit 1
fi

echo "Publishing Universal packages version: $UNIVERSAL_PACKAGE_VERSION (channel: $PACKAGE_CHANNEL)"

# Configure Azure DevOps CLI with organization and PAT token
echo "Configuring Azure DevOps CLI..."
export AZURE_DEVOPS_EXT_PAT="${SYSTEM_ACCESSTOKEN}"
az devops configure --defaults organization="${AZ_ORGANIZATION}" project="${AZ_PROJECT}"

# =============================================================================
# Publish function
# =============================================================================
publish_package() {
  local name="$1"
  local path="$2"
  local description="$3"
  
  if [ ! -d "$path" ]; then
    echo "Warning: Directory does not exist: $path (creating empty)"
    mkdir -p "$path"
  fi
  
  echo "Publishing $name version $UNIVERSAL_PACKAGE_VERSION..."
  az artifacts universal publish \
    --organization "$AZ_ORGANIZATION" \
    --project "$AZ_PROJECT" \
    --scope project \
    --feed "$AZ_ARTIFACT_FEED" \
    --name "$name" \
    --version "$UNIVERSAL_PACKAGE_VERSION" \
    --path "$path" \
    --description "$description"
  echo "✓ Published $name"
}

# =============================================================================
# Publish packages for each architecture
# =============================================================================
echo ""
echo "=============================================="
echo "Publishing universal packages to Azure Artifacts"
echo "=============================================="

for ARCH in "${ARCHITECTURES[@]}"; do
  echo ""
  echo "--- Architecture: $ARCH ---"
  
  BIN_DIR="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${ARCH}"
  
  publish_package \
    "${PACKAGE_BASE_NAME}-storage-${PACKAGE_CHANNEL}-${ARCH}" \
    "${BIN_DIR}/native-storage" \
    "DACS storage components (${ARCH})"
  
  publish_package \
    "${PACKAGE_BASE_NAME}-storage-cache-client-${PACKAGE_CHANNEL}-${ARCH}" \
    "${BIN_DIR}/native-storage-cache-client" \
    "DACS cache client library (${ARCH})"
done

echo ""
echo "✅ Universal packages published successfully"

