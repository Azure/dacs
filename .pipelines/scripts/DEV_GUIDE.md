# dacs - Developer Quick Guide

This guide provides quick reference for building, testing, and deploying the dacs system locally.

## Prerequisites

Before using these commands, ensure you have completed the setup steps in the [main README](../README.md), specifically the **Ubuntu 18.4** section under "Getting Started" for:
- Git and Git LFS installation
- Repository cloning
- Visual Studio Credential Provider setup
- Required dependencies

---

## Quick Start (5 Minutes)

```bash
# From repository root 
cd <path-to-dacs-repo>

# 1. Build everything
make build
make images

# 2. Run tests
make test                              # All unit tests
make test TEST=FileLogger              # Filtered unit tests

# 3. Deploy cacheserver to local Minikube
make deploy-minikube

# 4. Run integration tests
make perf-client MODE=upload           # Integration test - upload
make perf-client MODE=download         # Integration test - download
make storage-intercept-client          # Storage intercept test

# 5. Clean up cluster when done
make clean-minikube
```

**Note for WSL users:** Clone the repo in your Linux filesystem (e.g., `~/DACS`) rather than `/mnt/c/` for better performance.

---

## Build Commands

### Build Storage Components
```bash
make build
```
Builds CacheServer and client binaries (Debug mode by default).

### Build Docker Images
```bash
make images
```
Builds Docker images for CacheServer and CacheClient using locally built binaries.

### Clean Build
```bash
make clean
```
Removes `bin/` and `obj/` directories.

---

## Test Commands

### Unit Tests
```bash
# Run all unit tests
make test

# Run tests matching a pattern
make test TEST=FileLogger                 # Tests containing "FileLogger"
make test TEST=Cache                      # Tests containing "Cache"
make test TEST=StorageInterceptUnitTests  # Specific test binary
```

Uses Google Test framework with gtest filtering support.

---

## Deploy Commands

### Deploy to Minikube
```bash
# Full deployment: build + images + deploy + run perf tests
make deploy-minikube

# Skip build if already done
make deploy-minikube SKIP_BUILD=1
```

Creates a 4-node Kubernetes cluster locally using Minikube and deploys CacheServer with 3 replicas.

## Integration Test Commands

### Performance Tests
```bash
make perf-client MODE=upload
make perf-client MODE=download
```
Runs performance tests against deployed CacheServer.

### Storage Intercept Test
```bash
make storage-intercept-client
```
Tests storage intercept library functionality using Azure Blob Storage.

---

### Delete Cluster
```bash
make clean-minikube
```
Deletes the Minikube cluster.

---

## Configuration

### Minikube and Test Settings

To change Minikube cluster settings or performance test parameters, edit `scripts/k8s/config/local.config`.

### Storage Intercept Configuration

To configure Azure Storage settings for storage intercept tests, edit `scripts/k8s/config/storageIntercept.config`:
- Generate a SAS token from Azure Portal for your storage account
- Replace the placeholder SAS token in the config file with your generated token
- Update storage account name and container name as needed
---
