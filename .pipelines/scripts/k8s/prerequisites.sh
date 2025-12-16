#!/bin/bash

set -e

# Detect architecture (supports amd64, arm64)
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
  MINIKUBE_ARCH="amd64"
  KUBE_ARCH="amd64"
elif [ "$ARCH" = "aarch64" ]; then
  MINIKUBE_ARCH="arm64"
  KUBE_ARCH="arm64"
else
  echo "Unsupported architecture: $ARCH" >&2
  exit 1
fi

# Function to check if a command exists
check_command() {
  command -v "$1" >/dev/null 2>&1
}

# Install Docker
if ! check_command docker; then
  echo "Docker not found. Installing Docker..."
  sudo apt-get update
  sudo apt-get install -y docker.io
  sudo systemctl enable docker
  sudo systemctl start docker
  echo "Docker installed successfully."
else
  echo "Docker is already installed."
fi

# Install Minikube
if ! check_command minikube || ! minikube version >/dev/null 2>&1; then
  echo "Installing Minikube for arch $MINIKUBE_ARCH..."
  curl -LO "https://storage.googleapis.com/minikube/releases/latest/minikube-linux-${MINIKUBE_ARCH}"
  sudo install "minikube-linux-${MINIKUBE_ARCH}" /usr/local/bin/minikube
  rm "minikube-linux-${MINIKUBE_ARCH}"
  echo "Minikube installed successfully."
else
  echo "Minikube is already installed and runnable."
fi

# Install kubectl
if ! check_command kubectl || ! kubectl version --client >/dev/null 2>&1; then
  echo "Installing kubectl for arch $KUBE_ARCH..."
  curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/${KUBE_ARCH}/kubectl"
  sudo install kubectl /usr/local/bin/kubectl
  rm kubectl
  echo "kubectl installed successfully."
else
  echo "kubectl is already installed and runnable."
fi

# Install Helm
if ! check_command helm; then
  echo "Helm not found. Installing Helm..."
  curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
  echo "Helm installed successfully."
else
  echo "Helm is already installed."
fi

echo "All prerequisites are installed."