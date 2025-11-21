#!/bin/bash

set -e

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
if ! check_command minikube; then
  echo "Minikube not found. Installing Minikube..."
  curl -LO https://storage.googleapis.com/minikube/releases/latest/minikube-linux-amd64
  sudo install minikube-linux-amd64 /usr/local/bin/minikube
  rm minikube-linux-amd64
  echo "Minikube installed successfully."
else
  echo "Minikube is already installed."
fi

# Install kubectl
if ! check_command kubectl; then
  echo "kubectl not found. Installing kubectl..."
  curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
  sudo install kubectl /usr/local/bin/kubectl
  rm kubectl
  echo "kubectl installed successfully."
else
  echo "kubectl is already installed."
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