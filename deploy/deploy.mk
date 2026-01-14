# deploy.mk - AKS and Minikube deployment targets

.PHONY: help-deploy
help-deploy:
	@echo "Cluster Provisioning:"
	@echo "  make provision TARGET=<aks|minikube> 	- Create AKS cluster or Minikube"
	@echo "  make unprovision                     	- Delete cluster (auto-detects from kubectl context)"
	@echo ""
	@echo "Helm Deployment:"
	@echo "  make deploy                          	- Install all helm charts (prereq + cache-server)"
	@echo "  make undeploy                        	- Uninstall all helm charts"
	@echo ""
	@echo "AKS Options:"
	@echo "  make provision TARGET=aks            	- Create AKS cluster"
	@echo ""
	@echo "Minikube Options:"
	@echo "  make load-images                     	- Load images into Minikube"

##@ Infrastructure

# AKS deployment settings
AKS_TEMPLATE ?= nvme
AKS_LOCATION ?= eastus
AKS_RESOURCE_GROUP ?= $(USER)-cache-$(AKS_TEMPLATE)-$(AKS_LOCATION)
AKS_IS_TEST ?= false

# Image settings
DOCKER_REGISTRY ?= dacs.azurecr.io
COMMIT_HASH ?= $(shell git describe --always --dirty)
BUILD_NUMBER ?= latest
BASE_REPO ?= $(DOCKER_REGISTRY)
CACHE_SERVER_IMG ?= $(BASE_REPO)/cache-server
SCHEDULER_IMG ?= $(BASE_REPO)/cacheserver-scheduler
IMAGE_TAG ?= $(BUILD_NUMBER)-linux-amd64

# Tool versions
BICEP_VERSION ?= v0.37.4
HELM_VERSION ?= v3.18.6

# Architecture detection
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
    ARCH_SUFFIX := amd64
    BICEP_ARCH := x64
else ifeq ($(ARCH),aarch64)
    ARCH_SUFFIX := arm64
    BICEP_ARCH := arm64
else ifeq ($(ARCH),arm64)
    ARCH_SUFFIX := arm64
    BICEP_ARCH := arm64
else
    ARCH_SUFFIX := amd64
    BICEP_ARCH := x64
endif

# Local bin directory
LOCALBIN ?= $(shell pwd)/.local/bin
$(LOCALBIN):
	mkdir -p $(LOCALBIN)

BICEP ?= $(LOCALBIN)/bicep
HELM_BIN ?= $(LOCALBIN)/helm

##@ Cluster Provisioning

TARGET ?= minikube

# Detect cluster type from kubectl context (assume AKS if not minikube)
KUBE_CONTEXT := $(shell kubectl config current-context 2>/dev/null)
CLUSTER_TYPE := $(shell if echo "$(KUBE_CONTEXT)" | grep -q "minikube"; then echo "minikube"; else echo "aks"; fi)

.PHONY: provision
provision: ## Create cluster (TARGET=aks|minikube)
	@if [ -z "$(TARGET)" ]; then echo "Error: TARGET is required. Usage: make provision TARGET=<aks|minikube>"; exit 1; fi
	@if [ "$(TARGET)" = "aks" ]; then \
		$(MAKE) aks; \
	elif [ "$(TARGET)" = "minikube" ]; then \
		$(MAKE) minikube; \
	else \
		echo "Error: Invalid TARGET '$(TARGET)'. Must be 'aks' or 'minikube'"; exit 1; \
	fi

.PHONY: unprovision
unprovision: ## Delete cluster (auto-detects from kubectl context)
	@if [ "$(CLUSTER_TYPE)" = "minikube" ]; then \
		echo "Detected minikube cluster from context '$(KUBE_CONTEXT)'"; \
		$(MAKE) minikube-clean; \
	else \
		echo "Detected AKS cluster from context '$(KUBE_CONTEXT)'"; \
		$(MAKE) aks-clean; \
	fi

##@ AKS Deployment

.PHONY: aks
aks: bicep ## Create an AKS cluster with NVMe nodes using Bicep.
	@if [ ! -f deploy/parameters/$(AKS_TEMPLATE).json ]; then echo "deploy/parameters/$(AKS_TEMPLATE).json does not exist" && exit 1; fi
	@if [ -z "$(AKS_RESOURCE_GROUP)" ]; then echo "AKS_RESOURCE_GROUP is not set"; exit 1; fi
	@if [ -z "$(AKS_LOCATION)" ]; then echo "AKS_LOCATION is not set"; exit 1; fi
	./deploy/scripts/aks/create.sh --resource-group $(AKS_RESOURCE_GROUP) --location $(AKS_LOCATION) --template $(AKS_TEMPLATE) --is-test $(AKS_IS_TEST)

.PHONY: aks-cluster-requirements
aks-cluster-requirements: bicep ## Print the requirements for the AKS cluster (what-if).
	@if [ ! -f deploy/parameters/$(AKS_TEMPLATE).json ]; then echo "deploy/parameters/$(AKS_TEMPLATE).json does not exist" && exit 1; fi
	@if [ -z "$(AKS_RESOURCE_GROUP)" ]; then echo "AKS_RESOURCE_GROUP is not set"; exit 1; fi
	@if [ -z "$(AKS_LOCATION)" ]; then echo "AKS_LOCATION is not set"; exit 1; fi
	@./deploy/scripts/aks/create.sh --resource-group $(AKS_RESOURCE_GROUP) --location $(AKS_LOCATION) --template $(AKS_TEMPLATE) --is-test $(AKS_IS_TEST) --what-if

.PHONY: aks-clean
aks-clean: ## Delete the AKS cluster.
	@if [ -z "$(AKS_RESOURCE_GROUP)" ]; then echo "AKS_RESOURCE_GROUP is not set"; exit 1; fi
	./deploy/scripts/aks/clean.sh --resource-group $(AKS_RESOURCE_GROUP)

##@ Minikube (Local Development)

.PHONY: minikube
minikube: ## Create a local Minikube cluster.
	@echo "Creating Minikube cluster..."
	@./deploy/scripts/minikube/create.sh

.PHONY: load-images
load-images: ## Load images and deploy cache server to Minikube.
	@echo "Loading images and deploying to Minikube..."
	@./deploy/scripts/minikube/load-images.sh

.PHONY: minikube-clean
minikube-clean: ## Delete the Minikube cluster.
	@echo "Deleting Minikube cluster..."
	@./deploy/scripts/minikube/clean.sh

##@ Helm Charts

HELM_NAMESPACE ?= cache-server
HELM_ARGS ?=

# Local image settings for minikube
LOCAL_CACHE_SERVER_IMG ?= cache-server
LOCAL_CACHE_SERVER_TAG ?= local
LOCAL_SCHEDULER_IMG ?= cacheserver-scheduler
LOCAL_SCHEDULER_TAG ?= local

# Remote chart settings (ACR)
HELM_CHART_REPO ?= charts/cache-server
HELM_CHART_BASE_VERSION ?= $(shell grep '^version:' helm/cache-server/Chart.yaml | awk '{print $$2}')
HELM_CHART_VERSION ?= $(HELM_CHART_BASE_VERSION)-$(BUILD_NUMBER)

define helm_install
$(HELM_BIN) upgrade --install cache-server ./helm/cache-server \
	--namespace $(HELM_NAMESPACE) --create-namespace \
	--set cacheServer.image.repository=$(1) \
	--set cacheServer.image.tag=$(2) \
	--set cacheServer.scheduler.image.repository=$(3) \
	--set cacheServer.scheduler.image.tag=$(4) \
	--wait --timeout 10m --atomic $(HELM_ARGS)
endef

define helm_install_remote
$(HELM_BIN) upgrade --install cache-server oci://$(DOCKER_REGISTRY)/$(HELM_CHART_REPO) \
	--version $(HELM_CHART_VERSION) \
	--namespace $(HELM_NAMESPACE) --create-namespace \
	--set cacheServer.image.repository=$(CACHE_SERVER_IMG) \
	--set cacheServer.image.tag=$(IMAGE_TAG) \
	--set cacheServer.scheduler.image.repository=$(SCHEDULER_IMG) \
	--set cacheServer.scheduler.image.tag=$(IMAGE_TAG) \
	--wait --timeout 10m --atomic $(HELM_ARGS)
endef

# Login to ACR for Helm if the registry is an Azure Container Registry
define acr_login
@if echo "$(DOCKER_REGISTRY)" | grep -q "azurecr.io"; then \
	ACR_NAME=$$(echo "$(DOCKER_REGISTRY)" | cut -d. -f1); \
	echo "Logging into ACR $$ACR_NAME for Helm..."; \
	TOKEN=$$(az acr login --name "$$ACR_NAME" --expose-token --output tsv --query accessToken 2>/dev/null); \
	if [ -n "$$TOKEN" ]; then \
		echo "$$TOKEN" | $(HELM_BIN) registry login "$(DOCKER_REGISTRY)" --username 00000000-0000-0000-0000-000000000000 --password-stdin; \
	else \
		echo "Warning: Could not get ACR token, helm may fail if not already logged in"; \
	fi \
fi
endef

.PHONY: helm-prereq-install
helm-prereq-install: helm ## Deploy dacs-prereq chart for NVMe RAID setup.
	$(HELM_BIN) upgrade --install dacs-prereq ./helm/dacs-prereq \
		--namespace $(HELM_NAMESPACE) --create-namespace \
		--wait --timeout 10m --atomic $(HELM_ARGS)

.PHONY: helm-prereq-uninstall
helm-prereq-uninstall: helm ## Uninstall dacs-prereq chart.
	$(HELM_BIN) uninstall dacs-prereq --namespace $(HELM_NAMESPACE) --ignore-not-found --wait

.PHONY: helm-install
helm-install: helm ## Deploy cache-server chart (minikube: local images, AKS: remote ACR chart).
ifeq ($(CLUSTER_TYPE),minikube)
	$(call helm_install,$(LOCAL_CACHE_SERVER_IMG),$(LOCAL_CACHE_SERVER_TAG),$(LOCAL_SCHEDULER_IMG),$(LOCAL_SCHEDULER_TAG))
else
	$(call acr_login)
	@echo "Installing cache-server from $(DOCKER_REGISTRY)/$(HELM_CHART_REPO):$(HELM_CHART_VERSION)"
	$(call helm_install_remote)
endif

.PHONY: helm-install-remote
helm-install-remote: helm ## Deploy cache-server chart from remote ACR registry (explicit).
	@echo "Installing cache-server from $(DOCKER_REGISTRY)/$(HELM_CHART_REPO):$(HELM_CHART_VERSION)"
	$(call helm_install_remote)

.PHONY: helm-install-local
helm-install-local: helm ## Deploy cache-server chart from local ./helm/cache-server with local images.
	$(call helm_install,$(LOCAL_CACHE_SERVER_IMG),$(LOCAL_CACHE_SERVER_TAG),$(LOCAL_SCHEDULER_IMG),$(LOCAL_SCHEDULER_TAG))

.PHONY: helm-uninstall
helm-uninstall: helm ## Uninstall cache-server chart.
	$(HELM_BIN) uninstall cache-server --namespace $(HELM_NAMESPACE) --ignore-not-found --wait

##@ Unified Helm Deploy/Undeploy

.PHONY: deploy
deploy: helm ## Install all helm charts (prereq + cache-server for AKS, cache-server only for minikube).
ifeq ($(CLUSTER_TYPE),minikube)
	@$(MAKE) helm-install
else
	@$(MAKE) helm-prereq-install helm-install
endif
	@echo "Deployment complete."

.PHONY: undeploy
undeploy: helm ## Uninstall all helm charts.
ifeq ($(CLUSTER_TYPE),minikube)
	@$(MAKE) helm-uninstall
else
	@$(MAKE) helm-uninstall helm-prereq-uninstall
endif
	@echo "Undeployment complete."

##@ Dependencies

.PHONY: bicep
bicep: $(BICEP) ## Download bicep locally if necessary.
$(BICEP): $(LOCALBIN)
	@[ -f "$(BICEP)" ] || { \
	echo "Downloading bicep $(BICEP_VERSION) for $(ARCH) ($(BICEP_ARCH))..."; \
	curl --fail --retry 5 --retry-delay 10 --retry-connrefused -Lo bicep https://github.com/Azure/bicep/releases/download/$(BICEP_VERSION)/bicep-linux-$(BICEP_ARCH); \
	chmod +x bicep; \
	mkdir -p $(LOCALBIN); \
	mv bicep $(BICEP); \
	}

.PHONY: helm
helm: $(HELM_BIN) ## Download helm locally if necessary.
$(HELM_BIN): $(LOCALBIN)
	@[ -f "$(HELM_BIN)" ] || { \
	echo "Downloading helm $(HELM_VERSION) for $(ARCH) ($(ARCH_SUFFIX))..."; \
	curl --fail --retry 5 --retry-delay 10 --retry-connrefused -Lo helm.tar.gz https://get.helm.sh/helm-$(HELM_VERSION)-linux-$(ARCH_SUFFIX).tar.gz; \
	tar -zxvf helm.tar.gz; \
	mv linux-$(ARCH_SUFFIX)/helm $(HELM_BIN); \
	rm -rf linux-$(ARCH_SUFFIX) helm.tar.gz; \
	}
