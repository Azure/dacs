#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo Syncing build environment ...
dotnet msbuild dirs.proj -t:Restore -noConsoleLogger

# get Native container for Linux build
nativeBuildRepository="dacs.azurecr.io/native-build"
nativeBuildLabel=$(grep native-build-imagelabel build/.metadata/native-build.container.metadata | awk -F= '{print $2}')

echo Running Native Build Container $nativeBuildRepository:$nativeBuildLabel
docker run -it --rm \
    --privileged \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v /root/.dotnet:/root/.dotnet \
    -v ~:/root \
    -v $DIR:/repo \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v /usr/share/dotnet/sdk/NuGetFallbackFolder:/usr/share/dotnet/sdk/NuGetFallbackFolder \
    -w /repo \
    $nativeBuildRepository:$nativeBuildLabel /bin/bash
