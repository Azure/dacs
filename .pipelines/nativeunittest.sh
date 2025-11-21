#!/bin/bash

function execute_test() {
  local target=$1
  echo "Testing for ${target} projects......"

  echo "PWD = $DIR/../bin/$BUILD_CONFIGURATION/native//${target}/"
  cd $DIR/../bin/$BUILD_CONFIGURATION/native//${target}/
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
