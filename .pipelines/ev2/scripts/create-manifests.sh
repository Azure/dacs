#!/bin/bash
set -e

echo "=========================================="
echo "  Creating Multi-Platform Manifests"
echo "=========================================="
echo "Target ACR: ${TARGET_ACR}"
echo "Target Tag: ${TARGET_TAG}"
echo ""

# Login to target ACR
az acr login --name "$TARGET_ACR"

# List of DACS images (matching Corp pipeline)
IMAGES=(
  "cache-server"
  "cache-test-client"
  "cacheserver-scheduler"
)

# Create multi-platform manifest for each image
for image in "${IMAGES[@]}"; do
  echo ""
  echo "Creating manifest for: $image"
  
  MANIFEST_NAME="${TARGET_ACR}.azurecr.io/${image}:${TARGET_TAG}"
  AMD64_IMAGE="${TARGET_ACR}.azurecr.io/${image}:${TARGET_TAG}-linux-amd64"
  ARM64_IMAGE="${TARGET_ACR}.azurecr.io/${image}:${TARGET_TAG}-linux-arm64"
  
  # Create multi-platform manifest using buildx imagetools
  docker buildx imagetools create --tag "$MANIFEST_NAME" \
    "$AMD64_IMAGE" \
    "$ARM64_IMAGE"
  echo "  ✓ Multi-platform manifest created and pushed"
done

echo ""
echo "=========================================="
echo "  Manifest Creation Complete"
echo "=========================================="
