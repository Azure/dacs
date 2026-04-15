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

# Install Docker CE (Ubuntu's docker.io is stuck on 24.x / API 1.43, incompatible with newer daemons)
if ! check_command docker || ! docker buildx version &>/dev/null; then
  echo "Installing Docker CE..."
  sudo apt-get remove -y docker.io containerd runc moby-tini moby-engine moby-cli moby-compose moby-containerd moby-runc moby-buildx 2>/dev/null || true
  sudo install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg --yes
  sudo chmod a+r /etc/apt/keyrings/docker.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" \
    | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
  sudo apt-get update
  sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin
  sudo systemctl enable docker
  sudo systemctl start docker
  echo "Docker CE installed successfully."
else
  echo "Docker CE and Buildx are already installed."
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