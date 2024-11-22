#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
AMD_CODE_DIR="$SCRIPT_DIR/../bin/amd_code_copy"
DACS_ROOT="$SCRIPT_DIR/.."
AMD_CODE_PREPARED="false"

# Perform hipification, Copy native code to bin/amd_code_copy
# so that hipification does not modify local files
function __prepare_native_code_for_amd__(){
  rm -rf "$AMD_CODE_DIR"
  mkdir -p "$AMD_CODE_DIR"

  cp "$DACS_ROOT/CMakeLists.txt" "$AMD_CODE_DIR"
  cp -r "$DACS_ROOT/src" "$AMD_CODE_DIR"
  cp -r "$DACS_ROOT/build" "$AMD_CODE_DIR"

  time "$SCRIPT_DIR"/../.pipelines/amd/code_translation.py --root "$AMD_CODE_DIR"
}

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
  local python_deps=$2
  local latest_git_hash=`git rev-parse HEAD || echo "unknown_git_commit"`

  if [[ $cuda_deps == *"rocm"* ]]; then
    local AMD_BUILD="ON"
    PATH_TO_SOURCE="$AMD_CODE_DIR"
    if [[ "$AMD_CODE_PREPARED" == "false" ]]; then
      __prepare_native_code_for_amd__
      AMD_CODE_PREPARED="true"
    fi
  else
    local AMD_BUILD="OFF"
  fi

  echo "Build for ${cuda_deps} and python ${python_deps} projects......"
  invoke_cmake $CMAKE_RUNTIME_OUTPUT_DIRECTORY/${cuda_deps} $PATH_TO_SOURCE -DAMD_BUILD=$AMD_BUILD -DPYTHON_VERSION=${python_deps} -DCUDA_VERSION=${cuda_deps}
}

function cleanup_build() {
  local cuda_deps=$1
  echo "Clean build output for ${cuda_deps} projects......"
  CLEAN_BIN_PATH="$CMAKE_BINARY_DIR/${cuda_deps}"
  CLEAN_OBJ_PATH="$OBJ_DIR/${cuda_deps}"
  if [[ ! -f $CLEAN_BIN_PATH/build.ninja ]]; then
    echo Cleaning up /tmp$CLEAN_BIN_PATH
    rm -rf /tmp$CLEAN_BIN_PATH/*
    if [ $? -ne 0 ]; then
      echo Error: Cleanup of /tmp$CLEAN_BIN_PATH failed.
      exit 1
    fi
    echo Cleaning up /tmp$CLEAN_OBJ_PATH
    rm -rf /tmp$CLEAN_OBJ_PATH/*
    if [ $? -ne 0 ]; then
      echo Error: Cleanup of /tmp$CLEAN_OBJ_PATH failed.
      exit 1
    fi
  fi
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

# mitigation for the time sync issues
ntpdate time.windows.com
if [ $? -ne 0 ]
  then
    echo "WARNING: failed to synchronize time in the container."
fi

if [[ "$FAST_NATIVE_BUILD" == "true" ]]; then
  echo '*****************************'
  echo 'Performing fast build        '
  echo '*****************************'
  CMAKE_LIBRARY_OUTPUT_DIRECTORY_ORIG=CMAKE_LIBRARY_OUTPUT_DIRECTORY
  CMAKE_LIBRARY_OUTPUT_DIRECTORY=/tmp$CMAKE_LIBRARY_OUTPUT_DIRECTORY
  CMAKE_RUNTIME_OUTPUT_DIRECTORY_ORIG=CMAKE_RUNTIME_OUTPUT_DIRECTORY
  CMAKE_RUNTIME_OUTPUT_DIRECTORY=/tmp$CMAKE_RUNTIME_OUTPUT_DIRECTORY

  # if developer deleted Makefile in the bin/(Debug|Release)/native folder to do a clean build for example,
  # we should cleanup the tmp repo
  cleanup_build "non_cuda"

  if [[ -z "$DISABLE_PROFILER_LIB" || "$DISABLE_PROFILER_LIB" == "false" ]]; then
    cleanup_build "cuda_agnostic"
    cleanup_build "cuda_agnostic_interception"
  fi

  if [[ ! -z "$BUILD_CUDA_VERSION" ]]; then
    cleanup_build "$BUILD_CUDA_VERSION"
  else
    for cudaVersion in "11_1" "11_3" "11_4" "11_5" "11_6" "11_7"; do
      cleanup_build "cuda_$cudaVersion"
    done
  fi

  CMAKE_BINARY_DIR_ORIG=$CMAKE_BINARY_DIR
  CMAKE_BINARY_DIR=/tmp$CMAKE_BINARY_DIR

  # Sync source
  mkdir -p /tmp$PATH_TO_SOURCE
  cp $PATH_TO_SOURCE/CMakeLists.txt /tmp$PATH_TO_SOURCE/CMakeLists.txt

  IFS=':'
  read -ra dirs <<< "$SRC_SYNC_DIRS"
  for dir in "${dirs[@]}"; do
    execute_rsync $dir /tmp$dir -avu --delete
  done
  IFS=' '

  execute_rsync $CMAKE_BINARY_DIR_ORIG /tmp$CMAKE_BINARY_DIR -avu --delete --exclude=src --exclude=CMakeFiles

  execute_rsync $OBJ_DIR /tmp$OBJ_DIR -avu --delete

  PATH_TO_SOURCE=/tmp$PATH_TO_SOURCE
  cd $CMAKE_RUNTIME_OUTPUT_DIRECTORY
fi

execute_storage_build
if [[ "$BUILD_STORAGE_ONLY" == "true" ]]; then
  exit 0
fi

# Though we are looping over different python versions
# Device proxy should not be rebuilt and hence the build time
# would not be significantly higher
if [[ ! -z "$BUILD_PYTHON_VERSION" ]]; then
  execute_build "non_cuda" "$BUILD_PYTHON_VERSION"
else
  execute_build "non_cuda" "3.8"
  for pythonVersion in "3.6" "3.7" "3.9" "3.10" "3.11" "3.12"; do
    execute_build "python_only" "$pythonVersion"
  done
fi

if [[ -z "$DISABLE_PROFILER_LIB" || "$DISABLE_PROFILER_LIB" == "false" ]]; then
  pythonVersion="3.8"
  execute_build "cuda_agnostic" $pythonVersion
  execute_build "cuda_agnostic_interception" $pythonVersion
fi

if [[ ! -z "$BUILD_CUDA_VERSION" ]]; then
  pythonVersion="3.8"
  execute_build "$BUILD_CUDA_VERSION" $pythonVersion
else
  for cudaVersion in "11_1" "11_3" "11_4" "11_5" "11_6" "11_7"; do
    pythonVersion="3.8"
    execute_build "cuda_$cudaVersion" $pythonVersion
  done

  # AMD build disabled for now (unused)
  # for rocmVersion in "5_2_3"; do
  #   pythonVersion="3.8"
  #   execute_build "rocm_$rocmVersion" $pythonVersion
  # done
fi

if [[ "$FAST_NATIVE_BUILD" == "true" ]]; then
  execute_rsync $CMAKE_BINARY_DIR/ $CMAKE_BINARY_DIR_ORIG -avu --delete --exclude=src --exclude=CMakeFiles

  execute_rsync $CMAKE_LIBRARY_OUTPUT_DIRECTORY/ $CMAKE_REMOTE_PUBLISH_DIRECTORY -avu --delete
fi