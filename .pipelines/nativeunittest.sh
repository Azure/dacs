#!/bin/bash
function execute_test() {
  local cuda_deps=$1
  echo "Testing for ${cuda_deps} projects......"

  if [[ -e "$DIR/../build/SetupNativeUnitTestDependencies/setup_${cuda_deps}.sh" ]]; then
      . "$DIR/../build/SetupNativeUnitTestDependencies/setup_${cuda_deps}.sh"
  elif [[ ${cuda_deps} != "non_cuda" ]]; then
      echo "Failed to find setup script for cuda dependencies ${cuda_deps}"
      exit 1
  fi

  echo "PWD = $DIR/../bin/$BUILD_CONFIGURATION/native/${cuda_deps}/"
  cd $DIR/../bin/$BUILD_CONFIGURATION/native/${cuda_deps}/

  if [ ! -z "$TEST_NAME" ]; then
    ctest -V --no-tests=error -j -1 -R "$TEST_NAME"
  else
    ctest -V --no-tests=error -j -1
  fi

  if [ $? -ne 0 ]
  then
      exit 1
  fi
}

set -e

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

if [ -z "$2" ]
then
  TEST_NAME=
else
  TEST_NAME="$2"
fi


echo "Start running native unit tests"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

execute_test "non_cuda"

execute_test "cuda_10_1"

execute_test "cuda_11_3"

echo "End running native unit tests"
