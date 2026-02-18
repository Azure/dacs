# test.mk - E2E test targets

.PHONY: help-test
help-test:
	@echo "E2E Testing:"
	@echo "  make test-e2e                      	- Run all tests against current kubeconfig cluster"
	@echo "  make test-e2e TEST_FILTER=perf     	- Run only tests matching the 'perf' label"
	@echo "  make test-e2e TEST_FILTER=controller	- Run controller tests (needs dacs helm chart deployment)"
	@echo "  make get-support-bundle            	- Collect support bundle from cluster"
	@echo ""
	@echo "Test Options:"
	@echo "  TEST_FILTER=<label>                    - Filter tests by Ginkgo label (e2e, perf, controller)"
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
TEST_FILTER ?=
TEST_TIMEOUT ?= 60m

# Ginkgo binary
GINKGO_VERSION ?= v2.27.3
GINKGO ?= $(LOCALBIN)/ginkgo

# Kubectl binary (use system kubectl if available, otherwise download)
SYSTEM_KUBECTL := $(shell command -v kubectl 2>/dev/null)
KUBECTL ?= $(if $(SYSTEM_KUBECTL),$(SYSTEM_KUBECTL),$(LOCALBIN)/kubectl)

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

# Base label filter: exclude aks-only tests on minikube
BASE_LABEL_FILTER := $(if $(filter minikube,$(TEST_CLUSTER_TYPE)),!aks,)

# Build final label filter: combine TEST_FILTER with BASE_LABEL_FILTER
# Wrap TEST_FILTER in parentheses to ensure correct operator precedence when combined with BASE_LABEL_FILTER
COMPUTED_LABEL_FILTER := $(if $(TEST_FILTER),($(TEST_FILTER))$(if $(BASE_LABEL_FILTER), && $(BASE_LABEL_FILTER),),$(BASE_LABEL_FILTER))

# Extra arguments to pass to ginkgo test (e.g., -- --data-size-gib=100)
EXTRA_TEST_ARGS ?=

.PHONY: test-e2e
test-e2e: ginkgo kubectl ## Run tests against cluster. Use TEST_FILTER to filter by label.
	$(call run_e2e_tests,$(COMPUTED_LABEL_FILTER),./test/perf ./test/e2e)

define run_e2e_tests
GIT_COMMIT=$(GIT_COMMIT) GIT_BRANCH=$(GIT_BRANCH) PATH=$(LOCALBIN):$$PATH $(GINKGO) -v -r --timeout="$(TEST_TIMEOUT)" $(if $(1),--label-filter="$(1)",) --focus="$(FOCUS)" --no-color="$(NO_COLOR)" $(2) -- \
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
ifneq ($(SYSTEM_KUBECTL),)
	@echo "Using system kubectl: $(SYSTEM_KUBECTL)"
else
	@[ -f "$(KUBECTL)" ] || { \
	K8S_VERSION=$$(curl -L -s https://dl.k8s.io/release/stable.txt); \
	echo "Downloading latest stable kubectl for $${K8S_VERSION} $(TEST_ARCH) ($(TEST_ARCH_SUFFIX))..."; \
	curl --fail --retry 5 --retry-delay 10 --retry-connrefused -Lo kubectl "https://dl.k8s.io/release/$${K8S_VERSION}/bin/linux/$(TEST_ARCH_SUFFIX)/kubectl"; \
	chmod +x kubectl; \
	mkdir -p $(LOCALBIN); \
	mv kubectl $(KUBECTL); \
	}
endif

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
