#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
DACS_ROOT="$SCRIPT_DIR/.."

function invoke_cmake() {
  local latest_git_hash=`git rev-parse HEAD || echo "unknown_git_commit"`
  local cmake_build_dir=$1
  local source_dir=$2
  local cmake_extra_args=${@:3}

  mkdir -p $cmake_build_dir
  cd $cmake_build_dir
  echo "cmake \
    $cmake_extra_args \
    -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_CONFIGURATION \
    -DBUILD_WITH_DEBUG_SYMBOLS=$BUILD_WITH_DEBUG_SYMBOLS \
    -DGIT_COMMIT_HASH=${latest_git_hash} \
    -G Ninja \
    $source_dir"
  cmake \
    $cmake_extra_args \
    -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_CONFIGURATION \
    -DBUILD_WITH_DEBUG_SYMBOLS=$BUILD_WITH_DEBUG_SYMBOLS \
    -DGIT_COMMIT_HASH=${latest_git_hash} \
    -G Ninja \
    $source_dir

  ninja

  if [ $? -ne 0 ]; then
    exit 1
  fi
}

function execute_storage_build() {
  echo "Build for storage projects......"
  invoke_cmake $CMAKE_RUNTIME_OUTPUT_DIRECTORY/storage $PATH_TO_SOURCE -DENABLE_STORAGE_BUILD=true
}

function execute_build() {
  local cuda_deps=$1
  local latest_git_hash=`git rev-parse HEAD || echo "unknown_git_commit"`

  echo "Building ${cuda_deps}"
  invoke_cmake $CMAKE_RUNTIME_OUTPUT_DIRECTORY/${cuda_deps} $PATH_TO_SOURCE \
    -DAMD_BUILD=$AMD_BUILD \
    -DSKIP_SUBMODULE_BUILD=$SKIP_NATIVE_SUBMODULE_BUILD
}


PATH_TO_SOURCE="$(cd "$(dirname "$0")/.." && pwd )"
. "${PATH_TO_SOURCE}/build/LinuxCMakeProj.CommonUtils.sh"

# mitigation for container timing issue where current folder may not yet have write permissions
n=0
until [ "$n" -ge 5 ]
do
   touch CMakeCache.txt && break
   n=$((n+1))
   sleep 5s
done

if [ $? -ne 0 ]
  then
    echo "WARNING: failed to synchronize time in the container."
fi

execute_storage_build
