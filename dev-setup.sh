#!/bin/bash

set -e

echo "================================================"
echo "dacs - Development Environment Setup"
echo "================================================"
echo ""

# Update package lists
echo "[1/6] Updating package lists..."
sudo apt update

# Install build tools
echo "[2/6] Installing build tools..."
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  clang \
  make \
  git \
  git-lfs \
  jq
echo "    Build tools installed"

# Install Go (Microsoft build)
echo "[3/7] Installing Microsoft Go..."
if ! command -v go &> /dev/null; then
  # Add Microsoft package repository
  curl -sSL https://packages.microsoft.com/keys/microsoft.asc | sudo tee /etc/apt/trusted.gpg.d/microsoft.asc > /dev/null
  sudo apt-add-repository -y "https://packages.microsoft.com/ubuntu/$(lsb_release -rs)/prod"
  sudo apt update
  # Install Microsoft Go
  sudo apt install -y msft-golang
  echo "    Microsoft Go installed"
else
  echo "    Go already installed"
fi

# Initialize Git LFS
echo "[4/7] Initializing Git LFS..."
git lfs install
echo "    Git LFS initialized"

# Install Docker + Kubernetes tools (skips if already present)
echo "[5/7] Installing Docker and Kubernetes tools (minikube, kubectl, helm)..."
PREREQ_SCRIPT=".pipelines/scripts/k8s/prerequisites.sh"
if [ -f "$PREREQ_SCRIPT" ]; then
  bash "$PREREQ_SCRIPT"
else
  echo "Prerequisites script not found at $PREREQ_SCRIPT. Please run the Kubernetes tool installs manually."
fi

# Fix Docker permissions for current user
echo "[6/7] Configuring Docker permissions..."
if command -v docker &> /dev/null; then
  sudo usermod -aG docker $USER
  sudo chmod 666 /var/run/docker.sock 2>/dev/null || true
  echo "    Docker permissions configured"
fi

# Install Azure CLI (for CI/CD pipelines)
echo "[7/7] Installing Azure CLI..."
if ! command -v az &> /dev/null; then
  curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
  echo "    Azure CLI installed"
else
  echo "    Azure CLI already installed"
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
