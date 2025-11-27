.PHONY: help build clean test images deploy-minikube clean-minikube perf-client storage-intercept-client

help:
	@echo "dacs Build"
	@echo ""
	@echo "  make build                         	- Build storage (Debug)"
	@echo "  make build BUILD_TYPE=Release      	- Build storage (Release)"
	@echo "  make clean                         	- Clean build artifacts"
	@echo "  make clean BUILD_TYPE=Release      	- Clean Release build artifacts"
	@echo "  make test                          	- Run all unit tests"
	@echo "  make test TEST=<pattern>           	- Run specific unit tests"
	@echo "  make images                        	- Build Docker images"
	@echo "  make deploy-minikube               	- Build, create images, deploy, and run perf tests"
	@echo "  make deploy-minikube SKIP_BUILD=1  	- Deploy without rebuilding"
	@echo "  make perf-client MODE=<upload|download> - Run performance client tests"
	@echo "  make storage-intercept-client      	- Run storage intercept client tests"
	@echo "  make clean-minikube                	- Delete Minikube cluster"
	@echo ""
	@echo "Examples:"
	@echo "  make test"
	@echo "  make test TEST=StorageInterceptUnitTests"
	@echo "  make deploy-minikube"
	@echo "  make deploy-minikube SKIP_BUILD=1"
	@echo "  make perf-client MODE=upload"
	@echo "  make perf-client MODE=download"
	@echo "  make storage-intercept-client"

BUILD_TYPE ?= Debug
MODE ?= upload

build:
	@./bldenv.sh /bin/bash "./.pipelines/scripts/build/build-storage.sh $(BUILD_TYPE)"

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf bin/$(BUILD_TYPE) obj/$(BUILD_TYPE)
	@echo "Clean complete."

test:
	@./bldenv.sh /bin/bash "./.pipelines/nativeunittest.sh $(BUILD_TYPE) $(TEST)"

images:
	@echo "Copying binaries..."
	@BUILD_TYPE=$(BUILD_TYPE) ./.pipelines/scripts/build/copy-binaries.sh
	@echo "Building Docker images..."
	@./.pipelines/scripts/build/build-docker-images.sh

deploy-minikube:
ifndef SKIP_BUILD
	@echo "Building storage..."
	@$(MAKE) build
	@echo "Building images..."
	@$(MAKE) images
else
	@echo "Skipping build (SKIP_BUILD=1)"
endif
	@echo "Creating Minikube cluster..."
	@./.pipelines/scripts/k8s/create-minikube-cluster.sh
	@echo "Deploying cache server to Minikube..."
	@./.pipelines/scripts/k8s/deploy-cacheserver.sh
	@echo ""
	@echo "Running default performance tests..."
	@$(MAKE) perf-client MODE=upload
	@echo ""
	@$(MAKE) perf-client MODE=download

perf-client:
	@./.pipelines/scripts/k8s/run-perf-client.sh $(MODE)

storage-intercept-client:
	@./.pipelines/scripts/k8s/run-storageintercept-client.sh

clean-minikube:
	@echo "Deleting Minikube cluster..."
	@minikube delete