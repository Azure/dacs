
# =============================================================================
#
# THIS IS A GENERATED DOCKERFILE - DO NOT MODIFY
#
# Make changes to Dockerfile.template 
#
# =============================================================================
FROM ubuntu:22.04

USER root

ENV DEBIAN_FRONTEND=noninteractive \
  CMAKE_DEP_INSTALL_DIR=/dependency \
  NUGET_CREDENTIALPROVIDER_SESSIONTOKENCACHE_ENABLED=true

COPY cmake $CMAKE_DEP_INSTALL_DIR/cmake/

RUN \
    apt-get update && \
    apt-get install -yq python3 \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libibverbs-dev \
    libssl-dev \
    libboost-program-options-dev \
    curl \
    uuid-dev \
    ibverbs-providers \
    sysstat \
    tar \
    net-tools \
    pciutils \
    cmake \
    docker.io \
    wget \
    ethtool \
    pkg-config \
    iproute2 \
    vim \
    fuse3 \
    libfuse3-dev \
    apt-transport-https \
    gnupg \
    git \
    git-lfs \
    pass \
    gnupg-agent \
    software-properties-common \
    lsb-release \
    python3 \
    python3-dev \
    python3-pip \
    sudo \
    libboost-all-dev \
    libgoogle-perftools-dev \
    ninja-build \
    lld \
    clang \
    build-essential \
    gdb
  
RUN apt install -y python3-venv python3-pip python3-setuptools

RUN  sudo ln -s /usr/lib/x86_64-linux-gnu/libprofiler.so /usr/lib/libprofiler.so

RUN python3 -m venv /venv && . /venv/bin/activate &&  \
  python -m pip install --upgrade pip setuptools opencensus-ext-azure h5py

RUN curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

RUN wget https://go.dev/dl/go1.20.5.linux-amd64.tar.gz && \
  rm -rf /usr/local/go && tar -C /usr/local -xzf go1.20.5.linux-amd64.tar.gz
ENV PATH="/usr/local/go/bin:${PATH}"

RUN wget -qO- https://aka.ms/install-artifacts-credprovider.sh | bash

RUN cd /opt && wget https://builds.dotnet.microsoft.com/dotnet/Sdk/6.0.428/dotnet-sdk-6.0.428-linux-x64.tar.gz && \
  mkdir dotnet && \
  tar -C dotnet -xf dotnet-sdk-6.0.428-linux-x64.tar.gz && \
  rm -f dotnet-sdk-6.0.428-linux-x64.tar.gz

RUN wget https://github.com/PowerShell/PowerShell/releases/download/v7.5.0/powershell_7.5.0-1.deb_amd64.deb && \
  dpkg -i powershell_7.5.0-1.deb_amd64.deb || true && \
  apt-get install -f -y && \
  rm powershell_7.5.0-1.deb_amd64.deb

ENV PATH="/opt/dotnet:$PATH"

RUN curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash

RUN az extension add --name azure-devops

# Following is used to prebuild all DACS dependencies
RUN cd $CMAKE_DEP_INSTALL_DIR && \
    cmake -DCMAKE_BUILD_TYPE=Release -G Ninja $CMAKE_DEP_INSTALL_DIR/cmake && \
    ninja

#find all the dirs in /dependency and chmod 777,
#few dir name might have spaces in their name, so pass each line of the output of the find command to xargs instead of using space as separator
RUN  find /dependency -type d -print0 | xargs -0 -I {} chmod 777 "{}"

RUN cd ~


