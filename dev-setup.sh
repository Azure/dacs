#!/bin/bash

set -e

echo "================================================"
echo "dacs - Development Environment Setup"
echo "================================================"
echo ""

# Update package lists
echo "[1/4] Updating package lists..."
sudo apt update

# Install build tools
echo "[2/4] Installing build tools..."
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  clang \
  make \
  git \
  git-lfs
echo "    Build tools installed"

# Initialize Git LFS
echo "[3/5] Initializing Git LFS..."
git lfs install
echo "    Git LFS initialized"

# Install Docker + Kubernetes tools (skips if already present)
echo "[4/4] Installing Docker and Kubernetes tools (minikube, kubectl, helm)..."
PREREQ_SCRIPT=".pipelines/scripts/k8s/prerequisites.sh"
if [ -f "$PREREQ_SCRIPT" ]; then
  bash "$PREREQ_SCRIPT"
else
  echo "Prerequisites script not found at $PREREQ_SCRIPT. Please run the Kubernetes tool installs manually."
fi

echo ""
echo "================================================"
echo "All tools installed successfully!"
echo "================================================"
echo ""
echo "Next steps:"
echo "  1. Configure Git (if not already done):"
echo "     git config --global user.email \"you@example.com\""
echo "     git config --global user.name \"Your Name\""
echo ""
echo "  2. Restart WSL session for Docker permissions:"
echo "     exit"
echo "     Then in PowerShell/CMD: wsl -d Ubuntu"
echo ""
echo "  3. Start building:"
echo "     cd ~/DACS"
echo "     make build"
echo "     make test"
echo ""
