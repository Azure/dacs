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

# Build in module mode, not workspace mode: the repo-root go.work intentionally
# excludes this module (cache-controller's k8s.io/* MVS would force-upgrade us).
export GOWORK = off

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
