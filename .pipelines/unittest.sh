#!/bin/bash

set -e

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..
BASE_DIR="$(pwd)"

TEST_SCRIPT=$(find bin -iname Test-With-Retry.ps1)
TEST_RESULTS=$DIR/TestResults
TEST_RUNSETTINGS=.runsettings
TEST_CASE_ASSEMBLIES=$(find $BASE_DIR/bin -iname *Tests*dll -print | tr '\n' ,)
TEST_CASE_ASSEMBLIES=${TEST_CASE_ASSEMBLIES%?}

pwsh -Command "& $TEST_SCRIPT -TestAssemblies $TEST_CASE_ASSEMBLIES -TestFilters \"TestCategory=Unit&TestCategory!=Windows\" -RunSettingsPath $TEST_RUNSETTINGS -TestResultsPath $TEST_RESULTS -Linux"