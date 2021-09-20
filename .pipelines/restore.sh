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

dotnet msbuild dirs.proj -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=$BUILD_CONFIGURATION  -v:m -fileLogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.restore.log" $2

if [ $? -ne 0 ]
  then
    dotnet msbuild dirs.proj -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=$BUILD_CONFIGURATION -v:m -fileLogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.restore.2.log" $2
fi
