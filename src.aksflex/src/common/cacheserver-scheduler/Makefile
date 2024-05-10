# Test setup
.PHONY: test-setup
test-setup: fmt vet

# Run tests
.PHONY: test
test: test-setup
	go test -v  ./... -coverprofile cover.out

# Run go fmt against code
.PHONY: fmt
fmt:
	go fmt ./...

# Run go vet against code
.PHONY: vet
vet:
	go vet ./...
