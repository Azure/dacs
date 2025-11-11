#!/bin/bash -e


# Parse arguments
usage="Usage : $0 [--no-tty]"

TTY_OPTION="-t"

if [ "$1" == "--no-tty" ]; then
    TTY_OPTION=""
    shift
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# get Native container for Linux build
nativeBuildRepository="dacs.azurecr.io/native-build"
nativeBuildLabel=$(grep native-build-imagelabel build/.metadata/native-build.container.metadata | awk -F= '{print $2}')

echo Running Native Build Container $nativeBuildRepository:$nativeBuildLabel
docker run \
    --rm \
    -i \
    --ipc host \
    $TTY_OPTION \
    --privileged \
    --device /dev/fuse \
    --cap-add SYS_ADMIN \
    --security-opt apparmor:unconfined \
    --env VSS_NUGET_URI_PREFIXES \
    --env VSS_NUGET_ACCESSTOKEN \
    --env VSS_NUGET_EXTERNAL_FEED_ENDPOINTS \
    --env AZURE_DEVOPS_EXT_PAT \
    --env SI_azBlobAccessToken \
    --env STORAGE_INTERCEPT_CONFIG_FILEPATH \
    --env STORAGE_INTCPT_SAS_TOKEN \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v /root/.dotnet:/root/.dotnet \
    -v ~:/root \
    -v $HOME:$HOME \
    -v $DIR:$DIR \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v /usr/share/dotnet/sdk/NuGetFallbackFolder:/usr/share/dotnet/sdk/NuGetFallbackFolder \
    -v /tmp:/tmp \
    -w $DIR \
    $nativeBuildRepository:$nativeBuildLabel /bin/bash
