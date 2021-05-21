#!/bin/bash
function execute_build() {
  local cuda_deps=$1
  echo "Build for ${cuda_deps} projects......"

  mkdir -p $CMAKE_RUNTIME_OUTPUT_DIRECTORY/${cuda_deps}
  cd $CMAKE_RUNTIME_OUTPUT_DIRECTORY/${cuda_deps}

  cmake -DCUDA_DEPENDENCY=${cuda_deps} -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_CONFIGURATION $PATH_TO_SOURCE
  make -j $NATIVE_BUILD_JOBS

  if [ $? -ne 0 ]; then
    exit 1
  fi
}

function cleanup_build() {
  local cuda_deps=$1
  echo "Clean build output for ${cuda_deps} projects......"
  CLEAN_BIN_PATH="$CMAKE_BINARY_DIR/${cuda_deps}"
  CLEAN_OBJ_PATH="$OBJ_DIR/${cuda_deps}"
  if [[ ! -f $CLEAN_BIN_PATH/Makefile ]]; then
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

  cleanup_build "cuda_10_1"

  cleanup_build "cuda_11_0"

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

if [ -z $NATIVE_BUILD_JOBS ]; then
  NATIVE_BUILD_JOBS=12
fi

execute_build "non_cuda"

execute_build "cuda_10_1"

execute_build "cuda_11_0"

if [[ "$FAST_NATIVE_BUILD" == "true" ]]; then
  execute_rsync $CMAKE_BINARY_DIR/ $CMAKE_BINARY_DIR_ORIG -avu --delete --exclude=src --exclude=CMakeFiles

  execute_rsync $CMAKE_LIBRARY_OUTPUT_DIRECTORY/ $CMAKE_REMOTE_PUBLISH_DIRECTORY -avu --delete
fi