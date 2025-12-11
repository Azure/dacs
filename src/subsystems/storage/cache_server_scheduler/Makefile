# Test setup
.PHONY: test-setup
test-setup: fmt vet

# Run tests (use TEST_PATTERN to filter, e.g., make test TEST_PATTERN=TestScheduler)
.PHONY: test
test: test-setup
ifdef TEST_PATTERN
	go test -v -run $(TEST_PATTERN) ./... -coverprofile cover.out
else
	go test -v ./... -coverprofile cover.out
endif

# Run go fmt against code
.PHONY: fmt
fmt:
	go fmt ./...

# Run go vet against code
.PHONY: vet
vet:
	go vet ./...
