#!/bin/bash

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIR/..

git lfs install

# Install azure-devops extension for az cli if not already installed
if ! az extension show --name azure-devops > /dev/null 2>&1; then
  az extension add --name azure-devops
fi

dotnet msbuild dirs.proj -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=$BUILD_CONFIGURATION  -v:m -fileLogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.restore.log" $2

if [ $? -ne 0 ]
  then
    dotnet msbuild dirs.proj -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=$BUILD_CONFIGURATION -v:m -fileLogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.restore.2.log" $2
fi
