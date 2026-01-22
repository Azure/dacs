.PHONY: help build clean test pytest test-go test-native images storage-intercept-client helm-build helm-login helm-push deploy undeploy

# Include modular makefiles
include deploy/deploy.mk
include test/test.mk

help:
	@echo "dacs Build"
	@echo ""
	@echo "Build & Test:"
	@echo "  make build                         	- Build storage (Debug)"
	@echo "  make build BUILD_TYPE=Release      	- Build storage (Release)"
	@echo "  make clean                         	- Clean build artifacts"
	@echo "  make test                          	- Run all unit tests"
	@echo "  make test TEST=<pattern>           	- Run specific unit tests"
	@echo "  make pytest                        	- Run Python unit tests (pytest)"
	@echo "  make images                        	- Build Docker images"
	@echo ""
	@echo "Cluster Provisioning:"
	@echo "  make provision TARGET=<aks|minikube> 	- Create AKS cluster or Minikube"
	@echo "  make unprovision                     	- Delete cluster (auto-detects from kubectl context)"
	@echo ""
	@echo "Helm Deployment:"
	@echo "  make deploy                        	- Install cache-server helm charts"
	@echo "  make undeploy                      	- Uninstall cache-server helm charts"
	@echo ""
	@echo "E2E Testing:"
	@echo "  make test-e2e                      	- Run e2e tests against current kubeconfig"
	@echo ""
	@echo "Other:"
	@echo "  make storage-intercept-client      	- Run storage intercept client tests"
	@echo ""
	@echo "Examples:"
	@echo "  make build"
	@echo "  make images"
	@echo "  make provision TARGET=minikube"
	@echo "  make provision TARGET=aks AKS_TEMPLATE=nvme AKS_LOCATION=eastus"
	@echo "  make deploy"
	@echo "  make test-e2e"
	@echo "  make undeploy"
	@echo "  make unprovision"

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

test: test-native test-go

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

PYTEST_ARGS ?=

pytest:
	@if [ "$(USE_CONTAINER)" = "1" ]; then \
		echo "Using Container mode..." && \
		BUILD_ARCH=$(BUILD_ARCH) \
		STORAGE_INTCPT_SAS_TOKEN="$(STORAGE_INTCPT_SAS_TOKEN)" \
		SI_azBlobAccessToken="$(SI_azBlobAccessToken)" \
		AZURE_CLIENT_ID="$(AZURE_CLIENT_ID)" \
		USER_VM_AZURE_CLIENT_ID="$(USER_VM_AZURE_CLIENT_ID)" \
		./bldenv.sh /bin/bash "./.pipelines/pytest.sh $(BUILD_TYPE) $(PYTEST_ARGS)"; \
	else \
		BUILD_ARCH=$(BUILD_ARCH) ./.pipelines/pytest.sh $(BUILD_TYPE) $(PYTEST_ARGS); \
	fi

images:
	@echo "Copying binaries..."
	@BUILD_CONFIGURATION=$(BUILD_TYPE) BUILD_ARCH=$(BUILD_ARCH) ./.pipelines/scripts/build/copy-binaries.sh
	@echo "Building Docker images..."
	@./.pipelines/scripts/build/build-docker-images.sh

# For local use: publishes images and creates single-arch manifest
# Pipeline uses publish-images.sh directly (without manifest) then calls publish-multiplatform-manifests.sh
publish-images:
	@echo "Publishing Docker images..."
	@./.pipelines/scripts/build/publish-images.sh
	@echo "Creating manifest for local arch..."
	@BUILD_ARCHS="$$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')" ./.pipelines/scripts/build/publish-multiplatform-manifests.sh

storage-intercept-client:
	@./.pipelines/scripts/k8s/run-storageintercept-client.sh

helm-build:
	@./.pipelines/scripts/build/publish-helm-chart.sh

helm-login:
	@if [ -n "$$DOCKER_REGISTRY" ]; then \
		ACR_NAME=$$(echo "$$DOCKER_REGISTRY" | sed 's/\.azurecr\.io.*//'); \
		TOKEN=$$(az acr login --name "$$ACR_NAME" --expose-token --output tsv --query accessToken 2>/dev/null); \
		if [ -n "$$TOKEN" ]; then \
			echo "$$TOKEN" | helm registry login "$$DOCKER_REGISTRY" --username 00000000-0000-0000-0000-000000000000 --password-stdin; \
		fi \
	fi

helm-push: helm-login
	@./.pipelines/scripts/build/publish-helm-chart.sh --push