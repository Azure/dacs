# DACS Storage — Developer Guide

This repository contains the storage cache layer components for DACS, including CacheServer, CacheClient, and StorageInterception libraries.

For deployment and usage instructions, see the [User Guide](user-guide.md).

## Prerequisites

Before starting, ensure you have:
- **Windows 11** or **Windows 10 version 2004+** (for WSL2 support)
- **16GB RAM minimum** (recommended for building and testing)
- **50GB free disk space**

---

## Getting Started

### 1. Configure Authentication

When cloning over HTTPS in a headless WSL environment, Git Credential Manager cannot encrypt the Microsoft (MSAL) token cache and falls back to plaintext:

```text
warning: cannot persist Microsoft authentication token cache securely!
warning: using plain-text fallback token cache
```

The steps below install a Secret Service keyring so the token cache is stored securely. Run them once:

```bash
sudo apt update && sudo apt install -y libicu-dev pass gnupg gnome-keyring libsecret-1-0 libsecret-tools dbus-x11

# Generate a gpg key
# Use <your_alias>@users.noreply.github.com for the email
# Accepting the defaults is fine
gpg --full-generate-key

# Initialize password storage using the gpg-id from above
pass init <your_alias>@users.noreply.github.com

# Configure .gitconfig
git config --global credential.credentialStore gpg
git config --global credential.azreposCredentialType oauth

# Install git credential manager
GCM_DEB=$(curl -s https://api.github.com/repos/git-ecosystem/git-credential-manager/releases/latest \
| grep browser_download_url \
| grep 'gcm-linux-x64.*\.deb' \
| cut -d '"' -f 4)
curl -LO "$GCM_DEB"
sudo apt install -y "./$(basename "$GCM_DEB")"
unset GCM_DEB
git-credential-manager configure

# Create your keyring password:
read -rs -p "New keyring password: " _kr_pw; echo
eval "$(printf '%s' "$_kr_pw" | gnome-keyring-daemon --unlock --components=secrets)"
export GNOME_KEYRING_CONTROL SSH_AUTH_SOCK
unset _kr_pw
```

**Note:** If you ever need to change/reset your keyring password:
```bash
pgrep -au "$USER" gnome-keyring
kill <PID>
rm -f ~/.local/share/keyrings/login.keyring ~/.local/share/keyrings/user.keystore
# Create keyring password (above)
```

### 2. Configure keyring daemon on login:

Add the following to `~/.profile`:

```bash
# --- gnome-keyring unlock (WSL) ---
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
  eval "$(dbus-launch --sh-syntax)"
fi
# Only prompt if no keyring daemon is already running for this user
if ! pgrep -u "$USER" -x gnome-keyring-d >/dev/null 2>&1; then
  read -rs -p "Unlock login keyring: " _kr_pw; echo
  eval "$(printf '%s' "$_kr_pw" | gnome-keyring-daemon --unlock --components=secrets)"
  unset _kr_pw
  export GNOME_KEYRING_CONTROL SSH_AUTH_SOCK
fi
# --- end gnome-keyring ---

tty -s && export GPG_TTY=$(tty)
```

### 3. Clone Repository

```bash
cd ~
git clone https://github.com/Azure/dacs dacs
cd dacs
```
**Note for WSL users:** Always clone into the Linux filesystem (`~` or `/home/username`), not Windows paths (`/mnt/c`), for better I/O performance.

---

## Development Environments

Choose **one** of the two options below. Both provide identical development experiences.

### Option 1: Devcontainer (Recommended for VS Code users)

The devcontainer provides a pre-configured Docker-based development environment with all dependencies pre-installed.

**Prerequisites:**
- [Docker Desktop for Windows](https://docs.docker.com/desktop/install/windows-install/) (Enable "Use WSL 2 based engine" in Settings)
- [VS Code](https://code.visualstudio.com/Download)
- [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

**Setup Steps:**
1. Open the repository folder in VS Code
2. Run Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`) → **"Dev Containers: Reopen in Container"**
3. Wait for the container to build (~10-15 minutes on first run)
4. VS Code will reload inside the container when ready

**Before opening the devcontainer (host):**
```bash
git config --global user.email "you@example.com"
git config --global user.name "Your Name"
```
This creates `%USERPROFILE%\.gitconfig` (Windows) or `$HOME/.gitconfig` (Linux/macOS) so the devcontainer Git config mount succeeds.

**Note on Docker/Minikube in devcontainer:**
- The devcontainer uses **Docker-in-Docker (DinD)**, providing an isolated Docker daemon inside the container.
- This enables `make build` and `make images` to work reliably.
- Docker images built inside the devcontainer are **isolated from the host** (separate Docker daemon).
- **Minikube may work in devcontainer** but can be slow or unstable with multi-node clusters due to nested virtualization.
- **Recommended workflow:** Build and test in devcontainer, run `make deploy-minikube` on WSL host for best reliability.

**Verification:**
```bash
# Inside the devcontainer terminal
docker --version
go version
make --version
```

All dependencies (Docker, Go, Rust, build tools, Kubernetes tools) are pre-installed in the container image.

---

### Option 2: Local WSL2 Setup (Recommended for terminal users)

For developers who prefer working directly in WSL2 without VS Code.

**Prerequisites:**
- Windows with WSL2 enabled
- Ubuntu 22.04 or Ubuntu 24.04 installed in WSL2

**Step 1: Configure WSL2 Resources**

Create or edit `%USERPROFILE%\.wslconfig` on Windows (use Notepad):
```ini
[wsl2]
memory=16GB
processors=4
swap=4GB

[boot]
systemd=true
```

Restart WSL to apply changes:
```powershell
# In PowerShell or CMD (as Administrator)
wsl --shutdown
```

Wait 10 seconds, then restart WSL:
```powershell
wsl -d Ubuntu-22.04
```

**Step 2: Install Dependencies**

Navigate to the cloned repository and run the installation script:

```bash
cd ~/dacs
chmod +x dev-setup.sh
./dev-setup.sh
```

This script installs (~5 minutes):
- Build tools: `build-essential`, `cmake`, `ninja-build`, `clang`, `make`, `git`, `git-lfs`
- Docker and Docker CLI
- Kubernetes tools: `minikube`, `kubectl`, `helm`

**Step 3: Restart WSL Session (Required for Docker permissions)**

```bash
exit
```

Then in PowerShell/CMD:
```powershell
wsl -d Ubuntu-22.04
```

Navigate back to the project:
```bash
cd ~/dacs
```

**Verification:**
```bash
docker --version    # Should work without sudo
go version
make --version
```

---

## Building and Testing

All commands work identically in both devcontainer and WSL environments.

### Quick Start

```bash
# From repository root
make build                    # Build all components (Debug build, ~15-20 mins first time)
make test                     # Run unit tests (~2 mins)
make images                   # Build Docker images (~5 mins)
```

**First Build Note:** The initial `make build` takes 15-20 minutes because it:
- Builds the `native-build:dev` Docker image with all dependencies
- Downloads and compiles external dependencies (protobuf, fmt, etc.)
- Compiles all C++ source files

Subsequent builds are incremental and much faster (~2-5 minutes).

### Build Commands

```bash
make build                    # Debug build (default)
make build BUILD_TYPE=Release # Release build (optimized, smaller binaries)
make clean                    # Remove build artifacts
```

**Binary Output Locations:**
- Debug: `bin/Debug/native-storage/storage/`
- Release: `bin/Release/native-storage/storage/`

**Key Binaries:**
- `CacheServer` - Main cache server executable
- `CacheClient` - Cache client library test binary
- `libStorageIntercept.so` - Storage interception shared library
- `StorageInterceptUnitTests` - Unit test binary

### Unit Tests

```bash
make test                                   # Run all unit tests
make test TEST=FileLogger                   # Filter by test name (partial match)
make test TEST=Cache                        # Run cache-related tests
make test TEST=StorageInterceptUnitTests    # Run specific test binary
```

**Common Test Filters:**
- `TEST=CacheServer` - Cache server tests
- `TEST=StorageIntercept` - Storage interception tests
- `TEST=Snapshot` - Snapshot functionality tests

### Build Docker Images

```bash
make images                   # Build CacheServer and CacheClient images
```

Uses locally built binaries from the `bin/` directory. Images are tagged as:
- `cache-server:local`
- `cache-test-client:local`
- `cacheserver-scheduler:local`

---

## Local Kubernetes Deployment

Deploy CacheServer to a local Minikube cluster for integration and end-to-end testing.

**Note:** `make deploy-minikube` is **recommended to run on the WSL host** (not inside the devcontainer) due to potential networking issues with multi-node minikube clusters in nested Docker environments. You can still build and test components inside the devcontainer, but deployment to Kubernetes should be done on the host.

### Deploy to Minikube

```bash
make deploy-minikube                  # Build, create cluster, deploy, and run tests (~10-15 mins)
make deploy-minikube SKIP_BUILD=1     # Deploy only (skip build/images, ~5 mins)
```

**What this does:**
1. Creates a 4-node Minikube cluster (if not exists)
2. Loads Docker images into Minikube
3. Deploys CacheServer with 3 replicas using Helm charts
4. Configures port forwarding (localhost:9091 → CacheServer)
5. Runs performance client tests

**Minikube Cluster Details:**
- Nodes: 4 (1 control plane + 3 workers)
- CPUs per node: 2
- Memory per node: 2GB
- Driver: Docker

### Integration Tests

**Prerequisites:** CacheServer must be deployed (`make deploy-minikube`) before running integration tests.

```bash
# Performance tests
make perf-client MODE=upload          # Test upload performance
make perf-client MODE=download        # Test download performance

# Storage intercept test
make storage-intercept-client         # Test storage intercept functionality
```

**Storage Intercept Configuration:** Edit `.pipelines/scripts/k8s/config/storageIntercept.config` with your Azure Storage credentials:
```bash
# Example configuration
STORAGE_ACCOUNT=mystorageaccount
CONTAINER_NAME=mycontainer
SAS_TOKEN=sv=2021-06-08&ss=...
```

Generate a SAS token from [Azure Portal](https://portal.azure.com) → Storage Account → Shared access signature.

### Configuration Files

- **Minikube cluster settings**: `.pipelines/scripts/k8s/config/local.config`
- **Storage intercept settings**: `.pipelines/scripts/k8s/config/storageIntercept.config`
- **Helm values**: `helm/cache-server/values.yaml`

### Clean Up

```bash
make clean-minikube           # Delete Minikube cluster and free resources
```

**When to clean up:**
- After testing is complete
- Before switching branches
- If cluster becomes unstable

---

## Troubleshooting

### Common Issues

**1. "permission denied while trying to connect to Docker socket"**
```bash
# WSL only: Restart WSL session after running dev-setup.sh
exit
wsl -d Ubuntu-22.04
cd ~/dacs
```

**2. "make build" fails with "No space left on device"**
```bash
# Clean up Docker
docker system prune -a -f
# Clean build artifacts
make clean
# Rebuild
make build
```

**3. "Permission denied" on .pb.cc files during build**
```bash
# Fix ownership of generated protobuf files
sudo chown -R $USER:$USER src/ build/
# Clean and rebuild
make clean
make build
```

**4. "Image 'native-build:dev' failed to build"**
```bash
# Delete existing image and rebuild
docker rmi native-build:dev
make build
```

**5. Minikube fails to start**
```bash
# Delete and recreate cluster
make clean-minikube
make deploy-minikube
```
---
