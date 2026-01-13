#!/bin/bash
# publish-universal-packages.sh
# Publishes universal packages to Azure Artifacts and creates git tag
# Mirrors the old MSBuild UniversalPackagePublish.Build.targets logic

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# =============================================================================
# Configuration
# =============================================================================
AZ_ORGANIZATION="internal-reference-removed"
AZ_PROJECT="internal"
AZ_ARTIFACT_FEED="Storage-Cache-Feed"

PACKAGE_BASE_NAME="az-dacs"
OFFICIAL_CHANNEL="official"
DEV_CHANNEL="dev"

BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Release}"
ARCHITECTURES=("amd64" "arm64")

# =============================================================================
# Determine branch and package channel
# =============================================================================
GIT_BRANCH=$(git rev-parse --symbolic-full-name --abbrev-ref HEAD 2>/dev/null || echo "HEAD")

if [ "$GIT_BRANCH" == "HEAD" ] && [ -n "$BUILD_SOURCEBRANCH" ]; then
  if [[ "$BUILD_SOURCEBRANCH" == "refs/heads/main" ]]; then
    GIT_BRANCH="main"
  else
    GIT_BRANCH=$(echo "$BUILD_SOURCEBRANCH" | sed 's|refs/heads/||')
  fi
fi

if [ "$GIT_BRANCH" == "main" ]; then
  PACKAGE_CHANNEL="$OFFICIAL_CHANNEL"
else
  PACKAGE_CHANNEL="$DEV_CHANNEL"
fi

echo "Git branch: $GIT_BRANCH"
echo "Package channel: $PACKAGE_CHANNEL"

# =============================================================================
# Version calculation
# Format: MddHHmmss (month, day, hour, minute, second)
# Example: 1225140643 = December 25, 14:06:43
# Example: 113180831 = January 13, 18:08:31
# Leading zeros are stripped to comply with SemVer (no leading zeros allowed)
# =============================================================================
CURRENT_DATE_RAW=$(date +"%m%d%H%M%S")
# Convert to integer to remove leading zeros (bash arithmetic strips them)
CURRENT_DATE=$((10#${CURRENT_DATE_RAW}))
BRANCH_HASH=$([[ "$GIT_BRANCH" == "main" ]] && echo "0" || printf '%s' "$GIT_BRANCH" | cksum | cut -d' ' -f1 | head -c5)
# Main branch extracts revision from Build.BuildNumber (e.g., "20260112.6" -> "6"), feature branches use 0
PACKAGE_REVISION=$([[ "$GIT_BRANCH" == "main" ]] && echo "$BUILD_NUMBER" | cut -d '.' -f 2 || echo "0")
PACKAGE_VERSION="${CURRENT_DATE}.${BRANCH_HASH}.${PACKAGE_REVISION}"

echo "Package version: $PACKAGE_VERSION"

# =============================================================================
# Check Azure CLI and configure Azure DevOps
# =============================================================================
if ! command -v az &> /dev/null; then
  echo "Error: Azure CLI (az) is not installed"
  exit 1
fi

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
  
  echo "Publishing $name version $PACKAGE_VERSION..."
  az artifacts universal publish \
    --organization "$AZ_ORGANIZATION" \
    --project "$AZ_PROJECT" \
    --scope project \
    --feed "$AZ_ARTIFACT_FEED" \
    --name "$name" \
    --version "$PACKAGE_VERSION" \
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
    "${PACKAGE_BASE_NAME}-${PACKAGE_CHANNEL}-${ARCH}" \
    "${BIN_DIR}/native" \
    "DACS native libraries (${ARCH})"
  
  publish_package \
    "${PACKAGE_BASE_NAME}-storage-${PACKAGE_CHANNEL}-${ARCH}" \
    "${BIN_DIR}/native-storage" \
    "DACS storage components (${ARCH})"
  
  publish_package \
    "${PACKAGE_BASE_NAME}-storage-cache-client-${PACKAGE_CHANNEL}-${ARCH}" \
    "${BIN_DIR}/native-storage-cache-client" \
    "DACS cache client library (${ARCH})"
done

# =============================================================================
# Create and push git tag
# =============================================================================
echo ""
echo "=============================================="
echo "Creating git tag"
echo "=============================================="

TAG_NAME="v${PACKAGE_VERSION}"
TAG_MESSAGE="Published universal pacakge for ${PACKAGE_BASE_NAME}-${PACKAGE_CHANNEL} and ${PACKAGE_BASE_NAME}-storage-${PACKAGE_CHANNEL} with version ${PACKAGE_VERSION}"

echo "Tag: $TAG_NAME"

git config user.email "${GIT_USER_EMAIL:-azuredevops@users.noreply.github.com}"
git config user.name "${GIT_USER_NAME:-Azure DevOps Pipeline Agent}"
git tag -a "$TAG_NAME" -m "$TAG_MESSAGE"
git push origin "$TAG_NAME"

echo "✓ Pushed tag $TAG_NAME"

echo ""
echo "=============================================="
echo "✅ Complete! Published 6 packages + tag"
echo "=============================================="
