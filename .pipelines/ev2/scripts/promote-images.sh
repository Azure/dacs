#!/bin/bash
set -e

ARCHITECTURE=$1

echo "=========================================="
echo "  Image Promotion: ${ARCHITECTURE}"
echo "=========================================="
echo "Source ACR: ${SOURCE_ACR}"
echo "Target ACR: ${TARGET_ACR}"
echo "Source Tag: ${SOURCE_TAG}"
echo "Target Tag: ${TARGET_TAG}"
echo ""

# List of DACS images (matching Corp pipeline)
IMAGES=(
  "cache-server"
  "cache-test-client"
  "cacheserver-scheduler"
)

# Track if any image failed
HAS_FAILURES=false

# Import each image from source to target ACR
for image in "${IMAGES[@]}"; do
  SOURCE_IMAGE="${SOURCE_ACR}.azurecr.io/${image}:${SOURCE_TAG}-linux-${ARCHITECTURE}"
  TARGET_IMAGE="${image}:${TARGET_TAG}-linux-${ARCHITECTURE}"
  
  echo ""
  echo "Processing: $image"
  echo "  Import from: $SOURCE_IMAGE"
  
  # Import image from source to target ACR with new tag
  if az acr import \
    --name "$TARGET_ACR" \
    --source "$SOURCE_IMAGE" \
    --image "$TARGET_IMAGE" \
    --username "00000000-0000-0000-0000-000000000000" \
    --password "$SOURCE_TOKEN" \
    --force 2>&1; then
    echo "  ✓ Imported successfully as: ${TARGET_ACR}.azurecr.io/${TARGET_IMAGE}"
  else
    echo "  ✗ ERROR: Failed to import $SOURCE_IMAGE"
    HAS_FAILURES=true
  fi
done

if [ "$HAS_FAILURES" = true ]; then
  echo ""
  echo "=========================================="
  echo "  ✗ Image promotion failed"
  echo "=========================================="
  exit 1
fi

echo ""
echo "=========================================="
echo "  ${ARCHITECTURE} Image Promotion Complete"
echo "=========================================="
