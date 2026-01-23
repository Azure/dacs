# test.mk - E2E test targets

.PHONY: help-test
help-test:
	@echo "E2E Testing:"
	@echo "  make test-e2e                      	- Run e2e tests against current kubeconfig cluster (Old Controller)"
	@echo "  make test-e2e-controller           	- Run e2e tests with new controller deployment (CRD-based)"
	@echo "  make test-perf                     	- Run performance tests against current kubeconfig cluster"
	@echo "  make get-support-bundle            	- Collect support bundle from cluster"
	@echo ""
	@echo "Test Options:"
	@echo "  EXTRA_TEST_ARGS='--data-size-gib=100'  - Pass extra arguments to tests"
	@echo "  TEST_TIMEOUT=90m                       - Set test timeout (default: 60m)"
	@echo "  GIT_COMMIT=abc123                      - Override git commit for metrics"
	@echo "  GIT_BRANCH=main                        - Override git branch for metrics"

##@ Testing

# Test settings
SUPPORT_BUNDLE_OUTPUT_DIR ?= $(shell pwd)/support-bundles
TEST_OUTPUT ?= $(shell pwd)/test.xml
TEST_COVER ?= $(shell pwd)/coverage.lcov
SUMMARY_OUTPUT ?= $(shell pwd)/summary.md
NO_COLOR ?= false
FOCUS ?=
LABEL_FILTER ?=
TEST_TIMEOUT ?= 60m

# Ginkgo binary
GINKGO_VERSION ?= v2.27.3
GINKGO ?= $(LOCALBIN)/ginkgo

# Kubectl binary (downloads latest stable version)
KUBECTL ?= $(LOCALBIN)/kubectl

# Architecture detection
TEST_ARCH := $(shell uname -m)
ifeq ($(TEST_ARCH),x86_64)
    TEST_ARCH_SUFFIX := amd64
else ifeq ($(TEST_ARCH),aarch64)
    TEST_ARCH_SUFFIX := arm64
else ifeq ($(TEST_ARCH),arm64)
    TEST_ARCH_SUFFIX := arm64
else
    TEST_ARCH_SUFFIX := amd64
endif

# Git info for test metrics (can be overridden by pipeline)
GIT_COMMIT ?= $(shell git rev-parse HEAD 2>/dev/null || echo "<unknown>")
GIT_BRANCH ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "<unknown>")

# Detect cluster type from kubectl context (assume AKS if not minikube)
TEST_KUBE_CONTEXT := $(shell kubectl config current-context 2>/dev/null)
TEST_CLUSTER_TYPE := $(shell if echo "$(TEST_KUBE_CONTEXT)" | grep -q "minikube"; then echo "minikube"; else echo "aks"; fi)

# Set label filter based on cluster type (exclude aks-only tests on minikube)
E2E_LABEL_FILTER := $(if $(filter minikube,$(TEST_CLUSTER_TYPE)),e2e && !aks,e2e)
PERF_LABEL_FILTER := $(if $(filter minikube,$(TEST_CLUSTER_TYPE)),perf && !aks,perf)

# Extra arguments to pass to ginkgo test (e.g., -- --data-size-gib=100)
EXTRA_TEST_ARGS ?=

.PHONY: test-e2e
test-e2e: ginkgo kubectl ## Run e2e tests against cluster pointed to by kubeconfig.
	$(call run_e2e_tests,$(E2E_LABEL_FILTER),./test/e2e)

.PHONY: test-e2e-controller
test-e2e-controller: ginkgo kubectl ## Run e2e tests with controller deployment (sets INSTALL_CONTROLLER=true and filters to controller tests).
	INSTALL_CONTROLLER=true $(call run_e2e_tests,e2e && controller,./test/e2e)

.PHONY: test-perf
test-perf: ginkgo kubectl ## Run performance tests against cluster pointed to by kubeconfig.
	$(call run_e2e_tests,$(PERF_LABEL_FILTER),./test/perf)

define run_e2e_tests
GIT_COMMIT=$(GIT_COMMIT) GIT_BRANCH=$(GIT_BRANCH) PATH=$(LOCALBIN):$$PATH $(GINKGO) -v -r --timeout="$(TEST_TIMEOUT)" --label-filter="$(1)$(if $(LABEL_FILTER), && ($(LABEL_FILTER)))" --focus="$(FOCUS)" --no-color="$(NO_COLOR)" "$(2)" -- \
	--junit-report=$(TEST_OUTPUT) --summary-report=$(SUMMARY_OUTPUT) --support-bundle-dir=$(SUPPORT_BUNDLE_OUTPUT_DIR) $(EXTRA_TEST_ARGS)
endef

##@ Test Dependencies

.PHONY: ginkgo
ginkgo: $(GINKGO) ## Download ginkgo locally if necessary.
$(GINKGO): $(LOCALBIN)
	@[ -f "$(GINKGO)" ] || { \
	GOBIN=$(LOCALBIN) go install github.com/onsi/ginkgo/v2/ginkgo@$(GINKGO_VERSION); \
	}

.PHONY: kubectl
kubectl: $(KUBECTL) ## Download kubectl locally if necessary.
$(KUBECTL): $(LOCALBIN)
	@[ -f "$(KUBECTL)" ] || { \
	echo "Downloading latest stable kubectl for $(TEST_ARCH) ($(TEST_ARCH_SUFFIX))..."; \
	curl --fail --retry 5 --retry-delay 10 --retry-connrefused -Lo kubectl "https://dl.k8s.io/release/$$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/$(TEST_ARCH_SUFFIX)/kubectl"; \
	chmod +x kubectl; \
	mkdir -p $(LOCALBIN); \
	mv kubectl $(KUBECTL); \
	}

.PHONY: get-support-bundle
get-support-bundle: support-bundle ## Collect support bundle from the cluster.
	@mkdir -p $(SUPPORT_BUNDLE_OUTPUT_DIR)
	@echo "Collecting support bundle..."
	$(SUPPORT_BUNDLE) test/config/support-bundle.yaml \
		--interactive=false \
		-o $(if $(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT_DIR)/support-bundle.tar.gz)
	@tar -xvf $(if $(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT_DIR)/support-bundle.tar.gz) -C $(SUPPORT_BUNDLE_OUTPUT_DIR)
	@echo "Collecting Kubernetes events..."
	@kubectl get events --all-namespaces -o wide --sort-by='.lastTimestamp' > $(SUPPORT_BUNDLE_OUTPUT_DIR)/events.txt || echo "Failed to collect events" > $(SUPPORT_BUNDLE_OUTPUT_DIR)/events.txt
	@rm -f $(if $(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT),$(SUPPORT_BUNDLE_OUTPUT_DIR)/support-bundle.tar.gz)

# Support bundle CLI version
SUPPORT_BUNDLE_VERSION ?= v0.123.16
SUPPORT_BUNDLE ?= $(LOCALBIN)/support-bundle

.PHONY: support-bundle
support-bundle: $(SUPPORT_BUNDLE) ## Download support-bundle CLI locally if necessary.
$(SUPPORT_BUNDLE): $(LOCALBIN)
	@[ -f "$(SUPPORT_BUNDLE)" ] || { \
	echo "Downloading support-bundle $(SUPPORT_BUNDLE_VERSION) for $(TEST_ARCH) ($(TEST_ARCH_SUFFIX))..."; \
	curl --fail --retry 5 --retry-delay 10 --retry-connrefused -L \
		https://github.com/replicatedhq/troubleshoot/releases/download/$(SUPPORT_BUNDLE_VERSION)/support-bundle_linux_$(TEST_ARCH_SUFFIX).tar.gz | \
	tar -xvzf - support-bundle; \
	chmod +x support-bundle; \
	mkdir -p $(LOCALBIN); \
	mv support-bundle $(SUPPORT_BUNDLE); \
	}
