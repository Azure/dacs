#!/bin/bash

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

if [ -z "$2" ]
then
  BUILD_ARCHITECTURE=x86_64
else
  BUILD_ARCHITECTURE=$2
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIR/..

dotnet msbuild dirs.proj -t:Publish -maxCpuCount -p:TreatWarningsAsErrors=true -p:Configuration=$BUILD_CONFIGURATION -p:BuildArchitecture=$BUILD_ARCHITECTURE -t:Publish -v:n -filelogger -fileLoggerParameters:"logfile=$DIR/Log/msbuild.build.log"

if [ $? -ne 0 ]
  then
    exit 1
fi

# Build and publish Python package
chmod +x $DIR/build-python-package.sh
$DIR/build-python-package.sh --publish

echo Validate no source files modified. Any files found sent to stderr
echo git clean -x -d -n src
git clean -x -d -n src 1>&2