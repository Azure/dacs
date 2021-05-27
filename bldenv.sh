#!/bin/bash -e


# Parse arguments
usage="Usage : $0 [--no-tty]"

TTY_OPTION="-t"
RESTORE="true"

if [ "$1" == "--no-tty" ]; then
    TTY_OPTION=""
    shift
fi

if [ "$1" == "--no-restore" ]; then
    RESTORE="false"
    shift
fi


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [[ "$RESTORE" == "true" ]]; then
    echo Syncing build environment ...
    dotnet msbuild dirs.proj -t:Restore -noConsoleLogger
fi

# get Native container for Linux build
nativeBuildRepository="dacs.azurecr.io/native-build"
nativeBuildLabel=$(grep native-build-imagelabel build/.metadata/native-build.container.metadata | awk -F= '{print $2}')

echo Running Native Build Container $nativeBuildRepository:$nativeBuildLabel
docker run \
    --rm \
    -i \
    $TTY_OPTION \
    --privileged \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v /root/.dotnet:/root/.dotnet \
    -v ~:/root \
    -v $DIR:$DIR\
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v /usr/share/dotnet/sdk/NuGetFallbackFolder:/usr/share/dotnet/sdk/NuGetFallbackFolder \
    -w $DIR \
    $nativeBuildRepository:$nativeBuildLabel /bin/bash
