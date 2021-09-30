#!/bin/bash

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIR/..

dotnet msbuild dirs.proj -maxCpuCount -p:TreatWarningsAsErrors=true -p:Configuration=$BUILD_CONFIGURATION -t:Publish -v:n -filelogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.build.log"

if [ $? -ne 0 ]
  then
    exit 1
fi

echo Validate no source files modified. Any files found sent to stderr
echo git clean -x -d -n src
git clean -x -d -n src 1>&2