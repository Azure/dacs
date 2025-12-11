#!/bin/bash

set -e

# Optional test pattern (e.g., TestScheduler, TestReconcile)
TEST_PATTERN="${1:-}"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..
BASE_DIR="$(pwd)"

# Go project directories to test
GO_PROJECTS=(
    "src/subsystems/storage/cache_server_scheduler"
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
    fi
done
