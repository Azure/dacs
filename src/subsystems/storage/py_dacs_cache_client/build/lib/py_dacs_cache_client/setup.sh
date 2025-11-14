#!/bin/bash

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
echo ">>>>>>>>>>>>>>>>>>>>>>>>>> Working directory: $SCRIPT_DIR"

echo ">>>>>>>>>>>>>>>>>>>>>>>>>> Installing storage interception dependencies"

apt-get update && \
apt-get install -y --no-install-recommends -o Dpkg::Options::="--force-confdef" libssl-dev libcurl4-openssl-dev curl libjsoncpp-dev uuid-dev ibverbs-providers

if [ -f /etc/os-release ]; then
    if grep -q "Ubuntu" /etc/os-release; then
        if grep -q "VERSION_ID=\"20.04\"" /etc/os-release; then
            ln -s /usr/lib/x86_64-linux-gnu/libjsoncpp.so.1.9.5 /usr/lib/x86_64-linux-gnu/libjsoncpp.so.1
            wget https://nz2.archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb
            dpkg -i libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb || sudo apt-get install -f -y
        elif grep -q "VERSION_ID=\"22.04\"" /etc/os-release; then
            # Will add soon, if anything required. As of now, base image is working fine
        else
            echo "Skipping additional setup as the OS version is not supported"
        fi
    else
        echo "Skipping additional setup as the OS is not Ubuntu"
    fi
fi

echo ">>>>>>>>>>>>>>>>>>>>>>>>>> Installed storage interception dependencies"

echo ">>>>>>>>>>>>>>>>>>>>>>>>>> Installing python dependent packages"
python3 -m pip install requests
echo ">>>>>>>>>>>>>>>>>>>>>>>>>> Installed python dependent packages"

echo "\n\n\n\nNB: Please configure the proper Access Token or SAS during initialisation. \n\n\n"
