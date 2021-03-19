#!/bin/bash
# When calling execute_rsync, first 2 args must be source and destination,
# followed by options
function execute_rsync() {
  local src=$1
  local dst=$2
  mkdir -p $src
  mkdir -p $dst

  echo Executing rsync $*
  rsync $*

  if [ $? -ne 0 ]; then
    echo Error: rsync $* failed.
    exit 1
  fi
}

if [ -z "$1" ]
then
  echo Missing Required Command Line Parameter: PATH_TO_SOURCE
  exit 1
else
  PATH_TO_SOURCE=$1
fi

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
  if [[ ! -f $CMAKE_BINARY_DIR/Makefile ]]; then
    echo Cleaning up /tmp$CMAKE_BINARY_DIR
    rm -rf /tmp$CMAKE_BINARY_DIR/*
    if [ $? -ne 0 ]; then
      echo Error: Cleanup of /tmp$CMAKE_BINARY_DIR failed.
      exit 1
    fi
    echo Cleaning up /tmp$OBJ_DIR
    rm -rf /tmp$OBJ_DIR/*
    if [ $? -ne 0 ]; then
      echo Error: Cleanup of /tmp$OBJ_DIR failed.
      exit 1
    fi
  fi
  CMAKE_BINARY_DIR_ORIG=$CMAKE_BINARY_DIR
  CMAKE_BINARY_DIR=/tmp$CMAKE_BINARY_DIR

  # Sync source
  cp /repo/CMakeLists.txt /tmp/repo/CMakeLists.txt

  IFS=':'
  read -ra dirs <<< "$SRC_SYNC_DIRS"
  for dir in "${dirs[@]}"; do
    execute_rsync $dir /tmp$dir -avu --delete
  done
  IFS=' '

  execute_rsync $CMAKE_BINARY_DIR/ /tmp$CMAKE_BINARY_DIR -avu --delete --exclude=src --exclude=CMakeFiles

  execute_rsync $OBJ_DIR /tmp$OBJ_DIR -avu --delete

  PATH_TO_SOURCE=/tmp$PATH_TO_SOURCE
  cd $CMAKE_RUNTIME_OUTPUT_DIRECTORY
fi

if [ -z $NATIVE_BUILD_JOBS ]; then
  NATIVE_BUILD_JOBS=8
fi

cmake -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_CONFIGURATION $PATH_TO_SOURCE
make -j $NATIVE_BUILD_JOBS

if [ $? -ne 0 ]; then
  exit 1
fi

if [[ "$FAST_NATIVE_BUILD" == "true" ]]; then
  execute_rsync $CMAKE_BINARY_DIR/ $CMAKE_BINARY_DIR_ORIG -avu --delete --exclude=src --exclude=CMakeFiles
  execute_rsync /tmp$OBJ_DIR $OBJ_DIR -avu --delete
fi