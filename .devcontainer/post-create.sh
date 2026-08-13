#!/bin/bash
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

echo "========================================"
echo "Devcontainer Post-Create Setup"
echo "========================================"

# Create mount target dirs if missing so bind mounts succeed
echo "[1/2] Creating workspace directories..."
root_dir="$(pwd)"
targets=("${root_dir}/bin" \
         "${root_dir}/build" \
         "${root_dir}/.ccache")
for d in "${targets[@]}"; do
    mkdir -p "$d"
done
echo "    Directories created: bin, build, .ccache"

# Run prerequisites
echo "[2/2] Installing prerequisites (Docker, minikube, kubectl, helm)..."
bash .pipelines/scripts/k8s/prerequisites.sh

# Mark workspace as safe for Git
git config --global --add safe.directory /workspaces/dacs

# Configure Git user if not already configured
if ! git config --global user.email > /dev/null 2>&1; then
  echo "Configuring Git user for devcontainer..."
  git config --global user.email "developer@example.com"
  git config --global user.name "Developer"
  echo "Note: Git configured with default values. Update with: git config --global user.email 'your@email.com'"
else
  echo "Git user already configured: $(git config --global user.name) <$(git config --global user.email)>"
fi

# Fix ownership of container-baked directories (/dependency) for vscode user
# This ensures seamless builds whether run on host or in container
echo "Fixing /dependency permissions for vscode user..."
if command -v sudo >/dev/null 2>&1; then
  sudo chown -R vscode:vscode /dependency 2>/dev/null || true
  sudo chmod -R u+rwX /dependency 2>/dev/null || true
  
  # Fix ownership of workspace files (especially proto compiled files that may have wrong permissions)
  echo "Fixing workspace file permissions..."
  sudo chown -R vscode:vscode /workspaces/dacs/src 2>/dev/null || true
  sudo chown -R vscode:vscode /workspaces/dacs/build 2>/dev/null || true
fi

# Install pytest in the virtual environment
echo "Installing pytest in virtual environment..."
if [ -d "/venv" ]; then
  /venv/bin/pip install --quiet pytest pytest-timeout
  echo "✅ pytest installed in /venv"
fi

echo ""
echo "========================================"
echo "Setup complete! Ready to build."
echo "========================================"
echo ""
echo "Quick start:"
echo "  make build          # Build all components"
echo "  make test           # Run native unit tests"
echo "  make pytest         # Run Python unit tests"
echo "  make deploy-minikube # Deploy to Kubernetes"
echo ""
echo "✅ Docker: $(docker --version 2>/dev/null || echo 'Installing...')"
echo "⚓ Kubectl: $(kubectl version --client --short 2>/dev/null || echo 'Installing...')"
echo "🔧 Minikube: $(minikube version --short 2>/dev/null || echo 'Installing...')"
echo "🐍 Pytest: $(/venv/bin/pytest --version 2>/dev/null || echo 'Installing...')"
echo "  make build    # Build all components"
echo "  make test     # Run unit tests"
echo "  make pytest   # Run Python tests"
echo "  make images   # Build Docker images"
echo ""
