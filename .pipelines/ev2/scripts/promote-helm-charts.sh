#!/bin/bash
set -e

echo "=========================================="
echo "  Helm Chart Promotion"
echo "=========================================="
echo "Source ACR: ${SOURCE_ACR}"
echo "Target ACR: ${TARGET_ACR}"
echo "Source Tag: ${SOURCE_TAG}"
echo "Target Tag: ${TARGET_TAG}"
echo ""

# List of Helm charts
CHARTS=(
  "dacs-prereq"
  "cache-server"
  "dacs"
)

# Track if any chart failed
HAS_FAILURES=false

for chart in "${CHARTS[@]}"; do
  echo ""
  echo "Processing chart: $chart"
  
  # Find chart version containing SOURCE_TAG
  SOURCE_CHART_VERSION=$(az acr repository show-tags \
    --name "$SOURCE_ACR" \
    --repository "charts/${chart}" \
    --orderby time_desc \
    --output tsv | grep "${SOURCE_TAG}" | head -n 1)
  
  if [ -z "$SOURCE_CHART_VERSION" ]; then
    echo "  ✗ ERROR: Chart '${chart}' not found with tag containing ${SOURCE_TAG}"
    HAS_FAILURES=true
    continue
  fi
  
  echo "  Source version: $SOURCE_CHART_VERSION"
  
  # Extract base version (everything before the last hyphen)
  # Example: 0.1.0-20260226.1 -> 0.1.0
  BASE_VERSION="${SOURCE_CHART_VERSION%-*}"
  TARGET_CHART_VERSION="${BASE_VERSION}-${TARGET_TAG}"
  
  echo "  Target version: $TARGET_CHART_VERSION"
  echo "  Importing to target ACR..."
  
  # Import the chart from source to target ACR with new tag
  az acr import \
    --name "$TARGET_ACR" \
    --source "${SOURCE_ACR}.azurecr.io/acstor/charts/${chart}:${SOURCE_CHART_VERSION}" \
    --image "charts/${chart}:${TARGET_CHART_VERSION}" \
    --username "00000000-0000-0000-0000-000000000000" \
    --password "$SOURCE_TOKEN" \
    --force
  
  echo "  ✓ Chart promoted successfully"
done

if [ "$HAS_FAILURES" = true ]; then
  echo ""
  echo "=========================================="
  echo "  ✗ Helm chart promotion failed"
  echo "=========================================="
  exit 1
fi

echo ""
echo "=========================================="
echo "  Helm Chart Promotion Complete"
echo "=========================================="
