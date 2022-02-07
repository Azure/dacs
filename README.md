# DACS

This repository contains the native components of DACS, including device proxy, CRIU deduper, LFS interceptor and storage cache layer. 

## Getting Started

These steps need to be only run the first-time you setup your machine. Upon completion, you will have a copy of the project on your local machine for development and testing purposes.

### Windows 10

1. Install Visual Studio 2019, .Net Core SDK, and (optional, but highly recommended!) Visual Studio Code
    ```
    Visual Studio 2019 Workloads: ASP.Net and web development, Azure Development, Python Development, Linux Development with C++ 
    https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Enterprise&rel=16
    .Net Core SDK
    https://dotnet.microsoft.com/download/dotnet-core/thank-you/sdk-3.0.100-windows-x64-installer
    Visual Studio Code - (Default Install)
    https://code.visualstudio.com/Download
    ```

2. Install GIT and GIT LFS
    ```
    GIT Install
    https://git-scm.com/download/win
    - Take Defaults.
    - Consider changing Git default editor to Visual Studio Code
    ```

3. Clone the DACS repository

    ```
    internal-reference-removed
    Click 'Clone' (Upper Right of Page)
    Under IDE, Select Visual Studio or Visual Studio Code
    Follow Prompts to Login, Specify local path, Click Clone
    ```
    **Note**: The local path you designate will automatically include the 'DACS' subfolder. So if you choose 'c:\repo' as local path, the repository will be created under 'c:\repo\DACS'.

4. Install Prerequisites (one time only)
    ```
    File Explorer => <path to repository root>\bldenvcmd.cmd
    > git lfs install
    > git config --global user.email "you@users.noreply.github.com"
    > git config --global user.name "Your Name"
    > powershell Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope CurrentUser
    > powershell Install-Module -Name Az -AllowClobber -Scope CurrentUser
    ```

5. Install Docker
    ```
    https://download.docker.com/win/stable/Docker%20Desktop%20Installer.exe
    ```


### Ubuntu 18.4

1. Install GIT and GIT LFS
    ```
    $ sudo apt update
    $ sudo apt install git
    $ sudo apt install git-lfs
    ```

2. Clone the DACS repository
    ```
    $ git clone https://github.com/Azure/dacs.git
    ```

4. Install Prerequisites
    ```
    $ cd <path to repository root>
    $ git lfs install
    $ git config --global user.email "you@users.noreply.github.com"
    $ git config --global user.name "Your Name"
    ```

5. Install and Login with Visual Studio Credential Provider
    ```
    $ cd <path to repository root>
    $ curl -s https://raw.githubusercontent.com/microsoft/artifacts-credprovider/master/helpers/installcredprovider.sh | sudo bash
    $ dotnet restore --interactive /p:NugetInteractive="true"  (follow device login link to complete signin)
    ```

6. Install Visual Studio Code (optional, but highly recommended!)
    ```
    Visual Studio Code
    https://code.visualstudio.com/Download
    ```

## Build
These are the steps used to compile the DACS project.

### Windows 10

- Building from Visual Studio IDE
    ```
    File Explorer => <path to repository root>\bldenvide.cmd
    Build Menu => Build Solution
    ```
- or, Building from Command Prompt
    ```
    File Explorer => <path to repository root>\bldenvcmd.cmd
    > type 'b'
    ```

### Ubuntu 18.4

- Building from Command Prompt
   ```
   $ cd <path to repository root>
   $ ./bldenv.sh
   $ build_type=[Release|Debug] && .pipelines/restore.sh $build_type -p:NuGetInteractive="true" && .pipelines/build.sh $build_type
   ```

## Run C# unit tests 
These are the steps used to run C# unit tests.

### Windows 10

- Run from Command Prompt
    ```
    File Explorer => <path to repository root>\bldenvcmd.cmd
    > type 'ut'
    ```

### Ubuntu 18.4

- Run from Command Prompt
    ```
   $ cd <path to repository root>
   $ ./bldenv.sh
   $ .pipelines/unittest.sh [Release|Debug]
   ```

## Run native unit tests 
These are the steps used to run native unit tests.

### Windows 10

- Run from Command Prompt
    ```
    File Explorer => <path to repository root>\bldenvcmd.cmd
    > type 'nut'
    ```

### Ubuntu 18.4

- Run from Command Prompt
    ```
   $ cd <path to repository root>
   $ ./bldenv.sh
   $ .pipelines/nativeunittest.sh [Release|Debug]
   ```

## Updating native build container 

- Run from Command Prompt
    ```
   $ cd <path to repository root>
   $ dotnet restore src/images/native-build/native-build.dockerproj           
   $ dotnet msbuild -M -t:ContainerBuild src/images/native-build/native-build.dockerproj           
   ```
- Make sure that the final update to build/.metadata/native-build.container.metadata is included in your PR.
