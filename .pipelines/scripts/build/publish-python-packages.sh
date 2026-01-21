#!/bin/bash
# publish-python-packages.sh
# Publishes Python packages to Azure Artifacts PyPI feed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# =============================================================================
# Configuration
# =============================================================================
PYPI_REPO_URL="internal-reference-removed"
PYPI_USERNAME="internal"

# Required environment variables (set by parent script and pipeline)
if [ -z "$PYTHON_PACKAGE_VERSION" ]; then
  echo "Error: PYTHON_PACKAGE_VERSION not set"
  exit 1
fi

if [ -z "$PYTHON_PACKAGE_DIR" ]; then
  echo "Error: PYTHON_PACKAGE_DIR not set"
  exit 1
fi

echo "Publishing Python packages version: $PYTHON_PACKAGE_VERSION"
echo ""

# =============================================================================
# Setup Python and twine
# =============================================================================
if command -v python3 &> /dev/null; then
  PYTHON_CMD=python3
elif command -v python &> /dev/null; then
  PYTHON_CMD=python
else
  echo "❌ Error: Python not found"
  exit 1
fi

if [ -z "$AZURE_DEVOPS_EXT_PAT" ]; then
  echo "❌ Error: AZURE_DEVOPS_EXT_PAT not set"
  exit 1
fi

echo "Installing twine..."
"$PYTHON_CMD" -m pip install --quiet --upgrade twine

# =============================================================================
# Publish Python packages
# =============================================================================
for wheel in "$PYTHON_PACKAGE_DIR"/*.whl; do
  echo ""
  echo "Publishing: $(basename "$wheel")"
  
  "$PYTHON_CMD" -m twine upload \
    --repository-url "$PYPI_REPO_URL" \
    --username "$PYPI_USERNAME" \
    --password "$AZURE_DEVOPS_EXT_PAT" \
    "$wheel"
  
  echo "✅ Published successfully"
done

echo ""
echo "=============================================="
echo "✅ Python packages published successfully"
echo "=============================================="
