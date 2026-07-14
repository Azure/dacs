#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

set -e

# Run each module in plain module mode. The repo-root go.work excludes the
# scheduler, so `go tool cover` (run outside `make`) fails for it in workspace
# mode; cache-controller is self-contained and equally happy in module mode.
export GOWORK=off

# Optional test pattern (e.g., TestScheduler, TestReconcile)
TEST_PATTERN="${1:-}"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..
BASE_DIR="$(pwd)"

# Go project directories to test
GO_PROJECTS=(
    "src/subsystems/storage/cache_server_scheduler"
    "src/subsystems/cache-controller"
)

for project in "${GO_PROJECTS[@]}"; do
    PROJECT_PATH="$BASE_DIR/$project"
    
    if [ ! -d "$PROJECT_PATH" ]; then
        echo "Warning: Project directory not found: $PROJECT_PATH"
        continue
    fi
    
    cd "$PROJECT_PATH"
    echo "Running make test in $project..."
    if [ -n "$TEST_PATTERN" ]; then
        make test TEST_PATTERN="$TEST_PATTERN"
    else
        make test

        # Ensure minimum code coverage is met
        coverage=$(go tool cover -func=cover.out | grep total: | awk '{print $3}' | sed 's/%//')
        min_coverage=20.0 #TODO: increase to mandate more coverage
        # Compare floats using awk to avoid dependency on 'bc'
        if awk -v c="$coverage" -v m="$min_coverage" 'BEGIN { exit (c < m) ? 0 : 1 }'; then
            echo "Code coverage $coverage% is below the minimum required $min_coverage%."
            exit 1
        else
            echo "Code coverage $coverage% meets the minimum required $min_coverage%."
        fi
    fi
done
