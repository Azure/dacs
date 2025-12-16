#!/bin/bash -e

# Parse arguments
usage="Usage : $0 [--no-tty]"

TTY_OPTION="-t"

# Auto-detect if running without TTY (e.g., in CI/CD)
if [ ! -t 0 ]; then
    TTY_OPTION=""
fi

# Allow explicit --no-tty flag to override
if [ "$1" == "--no-tty" ]; then
    TTY_OPTION=""
    shift
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
WORKSPACE_NAME="$(basename "$DIR")"

# Use local native-build image
nativeBuildImage="native-build:dev"

# Ensure local image exists, build if missing
if ! docker image inspect "$nativeBuildImage" >/dev/null 2>&1; then
    echo "Image '$nativeBuildImage' not found. Building it now..."
    docker build -f src/images/native-build/Dockerfile -t native-build:dev src/images/native-build
fi

# If arguments provided, run them; otherwise start interactive shell
if [ $# -eq 0 ]; then
    CMD="/bin/bash"
else
    CMD="$@"
fi

echo Running Native Build Container $nativeBuildImage
docker run \
    --rm \
    -i \
    --ipc host \
    $TTY_OPTION \
    --privileged \
    --env AZURE_DEVOPS_EXT_PAT \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v ~:/root \
    -v $DIR:/workspaces/$WORKSPACE_NAME \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -w /workspaces/$WORKSPACE_NAME \
    $nativeBuildImage $CMD
