#!/bin/bash

function execute_test() {
  local target=$1
  echo "Testing for ${target} projects......"

  local arch_root=${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}
  # Run CTest from the CMake build directory where CTestTestfile.cmake is generated
  local build_root="$DIR/../build/${BUILD_CONFIGURATION}/${arch_root}"
  echo "PWD = ${build_root}"
  cd "${build_root}"
  log_file="`pwd`/Testing/Temporary/LastTest.log"

  if [ ! -z "$TEST_NAME" ]; then
    export GTEST_FILTER="*${TEST_NAME}*"
    ctest -V --no-tests=error -j -1
    unset GTEST_FILTER
  else
    ctest -V --no-tests=error -j -1
  fi

  if [ $? -ne 0 ]
  then
    log_count=`cat $log_file | wc -l`
    if [[ "$log_count" -ne 3 ]]; then # when all tests are skipped
      exit 1
    fi
  fi
}

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

# Build architecture (env override)
BUILD_ARCH=${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}

if [ -z "$2" ]
then
  TEST_NAME=
else
  TEST_NAME="$2"
fi

echo "Start running native unit tests"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

execute_test "storage"

echo "End running native unit tests"
