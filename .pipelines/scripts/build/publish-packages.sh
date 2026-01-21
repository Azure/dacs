#!/bin/bash
# publish-packages.sh
# Master script that orchestrates publishing of all package types and creates git tag

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# =============================================================================
# Version variables - passed from pipeline
# =============================================================================
# Validate required environment variables
if [ -z "$PYTHON_PACKAGE_VERSION" ] || [ -z "$PACKAGE_CHANNEL" ]; then
  echo "ERROR: Required environment variables not set!"
  echo "  PYTHON_PACKAGE_VERSION=${PYTHON_PACKAGE_VERSION:-<not set>}"
  echo "  PACKAGE_CHANNEL=${PACKAGE_CHANNEL:-<not set>}"
  exit 1
fi

# Export variables so child scripts inherit them
export PYTHON_PACKAGE_VERSION
export UNIVERSAL_PACKAGE_VERSION
export PACKAGE_CHANNEL

echo ""
echo "=============================================="
echo "Publishing Pipeline"
echo "=============================================="
echo "Python Package Version: $PYTHON_PACKAGE_VERSION"
echo "Universal Package Version: $UNIVERSAL_PACKAGE_VERSION"
echo "Channel: $PACKAGE_CHANNEL"
echo ""

# =============================================================================
# Step 1: Publish Universal Packages
# =============================================================================
echo "Step 1: Publishing Universal packages..."
"$SCRIPT_DIR/publish-universal-packages.sh"

# =============================================================================
# Step 2: Publish Python Packages
# =============================================================================
echo ""
echo "Step 2: Publishing Python packages..."
"$SCRIPT_DIR/publish-python-packages.sh"

# =============================================================================
# Step 3: Create Git Tag (only for official releases from main branch)
# =============================================================================
if [ "$PACKAGE_CHANNEL" = "official" ]; then
  echo ""
  echo "=============================================="
  echo "Creating git tag"
  echo "=============================================="

  TAG_NAME="v${PYTHON_PACKAGE_VERSION}"
  TAG_MESSAGE="Published packages version ${PYTHON_PACKAGE_VERSION}"

  echo "Tag: $TAG_NAME"
  echo "Message: $TAG_MESSAGE"

  git config user.email "${GIT_USER_EMAIL:-azuredevops@users.noreply.github.com}"
  git config user.name "${GIT_USER_NAME:-Azure DevOps Pipeline Agent}"
  git tag -a "$TAG_NAME" -m "$TAG_MESSAGE"
  git push origin "$TAG_NAME"

  echo "✓ Pushed tag $TAG_NAME"
else
  echo ""
  echo "=============================================="
  echo "Skipping git tag creation"
  echo "=============================================="
  echo "Git tags are only created for official channel (main branch)"
  echo "Current channel: $PACKAGE_CHANNEL"
fi

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "=============================================="
echo "✅ Publishing Complete!"
echo "   - Universal packages: Published"
echo "   - Python packages: Published"
if [ "$PACKAGE_CHANNEL" = "official" ]; then
  echo "   - Git tag: v${PYTHON_PACKAGE_VERSION}"
else
  echo "   - Git tag: Skipped (dev channel)"
fi
echo "=============================================="
