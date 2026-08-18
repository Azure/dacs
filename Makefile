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

.PHONY: help build clean test pytest test-go test-native test-rust-client test-rust-client-integration rust-package images python-package storage-intercept-client helm-build helm-login helm-push deploy undeploy

# Include modular makefiles
include deploy/deploy.mk
include test/test.mk

help:
	@echo "DACS Storage Build"
	@echo ""
	@echo "Build & Test:"
	@echo "  make build                         	- Build storage (Debug)"
	@echo "  make build BUILD_TYPE=Release      	- Build storage (Release)"
	@echo "  make clean                         	- Clean build artifacts"
	@echo "  make test                          	- Run all unit tests"
	@echo "  make test TEST=<pattern>           	- Run specific unit tests"
	@echo "  make pytest                        	- Run Python unit tests (pytest)"
	@echo "  make test-rust-client              	- Run Rust client unit tests"
	@echo "  make test-rust-client-integration  	- Run Rust client integration tests"
	@echo "  make rust-package                  	- Build architecture-specific Rust client crate"
	@echo "  make images                        	- Build Docker images"
	@echo ""
	@echo "Cluster Provisioning:"
	@echo "  make provision TARGET=<aks|minikube> 	- Create AKS cluster or Minikube"
	@echo "  make unprovision                     	- Delete cluster (auto-detects from kubectl context)"
	@echo ""
	@echo "Helm Deployment:"
	@echo "  make deploy                        	- Deploy cache-server helm chart (default: CONTROLLER=old)"
	@echo "  make deploy CONTROLLER=new         	- Deploy dacs helm chart (new controller)"
	@echo "  make undeploy                      	- Uninstall deployed helm charts"
	@echo ""
	@echo "E2E Testing:"
	@echo "  make test-e2e                      	- Run all tests (e2e + perf)"
	@echo "  make test-e2e TEST_FILTER=<label>  	- Run tests matching label (perf, controller, etc.)"
	@echo ""
	@echo "Other:"
	@echo "  make storage-intercept-client      	- Run storage intercept client tests"
	@echo "  make teardown-raid                 	- Tear down NVMe RAID on all nodes (destructive)"
	@echo ""
	@echo "Examples:"
	@echo "  make build"
	@echo "  make images"
	@echo "  make provision TARGET=minikube"
	@echo "  make provision TARGET=aks AKS_TEMPLATE=nvme AKS_LOCATION=eastus"
	@echo "  make deploy"
	@echo "  make deploy CONTROLLER=new"
	@echo "  make test-e2e TEST_FILTER=perf"
	@echo "  make undeploy"
	@echo "  make unprovision"

## Location to install dependencies to
LOCALBIN ?= $(shell pwd)/bin
## Build configuration
BUILD_TYPE ?= Debug
BUILD_ARCH ?= $(shell uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')
MODE ?= upload
USE_CONTAINER ?= 1

build:
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		./bldenv.sh /bin/bash "./.pipelines/scripts/build/build.sh $(BUILD_TYPE) $(BUILD_ARCH)"; \
	else \
		./.pipelines/scripts/build/build.sh $(BUILD_TYPE) $(BUILD_ARCH); \
	fi

clean:
	@echo "Cleaning build artifacts..."
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		./bldenv.sh chmod -R u+w bin 2>/dev/null || true; \
		./bldenv.sh rm -rf bin/$(BUILD_TYPE)/$(BUILD_ARCH); \
	else \
		chmod -R u+w bin 2>/dev/null || true; \
		rm -rf bin/$(BUILD_TYPE)/$(BUILD_ARCH); \
	fi
	@echo "Clean complete."

test: test-native test-go test-rust-client

test-native:
	@echo "Running native unit tests..."
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		BUILD_ARCH=$(BUILD_ARCH) ./bldenv.sh /bin/bash "./.pipelines/nativeunittest.sh $(BUILD_TYPE) $(TEST)"; \
	else \
		BUILD_ARCH=$(BUILD_ARCH) ./.pipelines/nativeunittest.sh $(BUILD_TYPE) $(TEST); \
	fi

test-go: 
	@echo "Running Go unit tests..."
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		./bldenv.sh /bin/bash "./.pipelines/unittest_go.sh $(TEST)"; \
	else \
		./.pipelines/unittest_go.sh $(TEST); \
	fi

test-rust-client:
	@./bldenv.sh cargo test --locked --manifest-path src/subsystems/storage/rust/dacs_client/Cargo.toml --test unit_tests

test-rust-client-integration:
	@test -n "$(DACS_TEST_BLOB_PATH)" || { echo "DACS_TEST_BLOB_PATH is required"; exit 1; }
	@DACS_LIB_PATH="$(DACS_LIB_PATH)" DACS_TEST_BLOB_PATH="$(DACS_TEST_BLOB_PATH)" \
		./bldenv.sh cargo test --locked --manifest-path src/subsystems/storage/rust/dacs_client/Cargo.toml --test integration_tests -- --test-threads=1

rust-package:
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		BUILD_ARCH=$(BUILD_ARCH) \
		BUILD_ARCHITECTURE=$(BUILD_ARCH) \
		BUILD_CONFIGURATION=$(BUILD_TYPE) \
		./bldenv.sh ./.pipelines/scripts/build/build-rust-package.sh; \
	else \
		BUILD_ARCHITECTURE=$(BUILD_ARCH) \
		BUILD_CONFIGURATION=$(BUILD_TYPE) \
		./.pipelines/scripts/build/build-rust-package.sh; \
	fi

PYTEST_ARGS ?=

pytest:
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		echo "Using Container mode..." && \
		BUILD_ARCH=$(BUILD_ARCH) \
		STORAGE_INTCPT_SAS_TOKEN="$(STORAGE_INTCPT_SAS_TOKEN)" \
		SI_azBlobAccessToken="$(SI_azBlobAccessToken)" \
		SI_remoteBlobUseSdkClient="$(SI_remoteBlobUseSdkClient)" \
		SI_remoteBlobChecksumEnabled="$(SI_remoteBlobChecksumEnabled)" \
		AZURE_CLIENT_ID="$(AZURE_CLIENT_ID)" \
		USER_VM_AZURE_CLIENT_ID="$(USER_VM_AZURE_CLIENT_ID)" \
		AZURE_TENANT_ID="$(AZURE_TENANT_ID)" \
		AZURE_FEDERATED_TOKEN="$(AZURE_FEDERATED_TOKEN)" \
		AZURE_BLOB_PATH="$(AZURE_BLOB_PATH)" \
		./bldenv.sh /bin/bash "./.pipelines/pytest.sh $(BUILD_TYPE) $(PYTEST_ARGS)"; \
	else \
		BUILD_ARCH=$(BUILD_ARCH) ./.pipelines/pytest.sh $(BUILD_TYPE) $(PYTEST_ARGS); \
	fi

images:
	@echo "Copying binaries..."
	@BUILD_CONFIGURATION=$(BUILD_TYPE) BUILD_ARCH=$(BUILD_ARCH) ./.pipelines/scripts/build/copy-binaries.sh
	@echo "Building Docker images..."
	@./.pipelines/scripts/build/build-docker-images.sh

## Python wheel build configuration (CI placeholders by default)
PYTHON_PACKAGE_VERSION ?= 0.0.0
PACKAGE_CHANNEL ?= dev

# Build the dacs-client wheel from the compiled native library. Runs in the
# native-build container (USE_CONTAINER=1) so the wheel links against the same
# glibc the .so was built with. The version/channel are forwarded into the
# container via bldenv.sh's --env allowlist (BUILD_ARCHITECTURE defaults to the
# container's uname inside build-python-package.sh).
python-package:
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		echo "Building Python wheel (container mode)..." && \
		BUILD_ARCH=$(BUILD_ARCH) PYTHON_PACKAGE_VERSION=$(PYTHON_PACKAGE_VERSION) PACKAGE_CHANNEL=$(PACKAGE_CHANNEL) ./bldenv.sh /bin/bash "./.pipelines/scripts/build/build-python-package.sh"; \
	else \
		BUILD_ARCHITECTURE=$(BUILD_ARCH) PYTHON_PACKAGE_VERSION=$(PYTHON_PACKAGE_VERSION) PACKAGE_CHANNEL=$(PACKAGE_CHANNEL) ./.pipelines/scripts/build/build-python-package.sh; \
	fi

# For local use: publishes images and creates single-arch manifest
# Pipeline uses publish-images.sh directly (without manifest) then calls publish-multiplatform-manifests.sh
publish-images:
	@echo "Publishing Docker images..."
	@./.pipelines/scripts/publish/publish-images.sh
	@echo "Creating manifest for local arch..."
	@BUILD_ARCHS="$$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')" ./.pipelines/scripts/publish/publish-multiplatform-manifests.sh

storage-intercept-client:
	@./.pipelines/scripts/k8s/run-storageintercept-client.sh

helm-build:
	@./.pipelines/scripts/publish/publish-helm-chart.sh

helm-login:
	@if [ -n "$$DOCKER_REGISTRY" ]; then \
		ACR_NAME=$$(echo "$$DOCKER_REGISTRY" | sed 's/\.azurecr\.io.*//'); \
		TOKEN=$$(az acr login --name "$$ACR_NAME" --expose-token --output tsv --query accessToken 2>/dev/null); \
		if [ -n "$$TOKEN" ]; then \
			echo "$$TOKEN" | helm registry login "$$DOCKER_REGISTRY" --username 00000000-0000-0000-0000-000000000000 --password-stdin; \
		fi \
	fi

helm-push: helm-login
	@./.pipelines/scripts/publish/publish-helm-chart.sh --push
