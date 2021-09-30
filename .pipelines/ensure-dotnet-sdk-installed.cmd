@if not defined _echo echo off
@setlocal EnableDelayedExpansion
@setlocal enableextensions
REM This script installs .NET Core SDK on Windows

SET RequiredMajorMinor=3.1
SET RequiredBuild=402
SET dotNetInstaller=dotnet-sdk-%RequiredMajorMinor%.%RequiredBuild%-win-x64.exe

REM This Url can be obtained from the https://dotnet.microsoft.com/download/dotnet-core/thank-you/sdk-3.1.300-windows-x64-installer page (replace sdk-3.1.300 with desired version)
SET downloadUrl=https://download.visualstudio.microsoft.com/download/pr/00000000-0000-0000-0000-000000000000/1f90fd18eb892cbb0bf75d9cff377ccb/%dotNetInstaller%

REM #######################################################
REM # Check if required .NET Core SDK is already installed.
REM #######################################################
SET SdksDir=%ProgramFiles%\dotnet\sdk
SET /A ver=%RequiredBuild%

FOR /l %%i in (0, 1, 50) do (
  IF EXIST "!SdksDir!\%RequiredMajorMinor%.!ver!" (
    REM ECHO Found .NET Core SDK v%RequiredMajorMinor%.!ver!
    EXIT /b 0
  )
  SET /A ver+=1
)

REM #######################################################
REM # Download .NET Core SDK installer.
REM #######################################################
ECHO .NET Core SDK v%RequiredMajorMinor%.%RequiredBuild% not found. Installing .NET Core SDK

SET scriptPath=%~dp0
SET dotNetInstallerPath=%scriptPath%%dotNetInstaller%
ECHO Downloading .NET Core SDK installer from %downloadUrl%
powershell -noprofile -ExecutionPolicy Bypass -Command "(New-Object Net.WebClient).DownloadFile('%downloadUrl%', '%dotNetInstallerPath%')"

IF '%ERRORLEVEL%' NEQ '0' (
  ECHO Error: Failed to download .NET Core SDK installer from %downloadUrl%
  EXIT /b %ERRORLEVEL%
)

REM #######################################################
REM # Execute .NET Core SDK installer.
REM #######################################################
echo Executing %dotNetInstallerPath% /install /passive
%dotNetInstallerPath% /install /passive

IF '%ERRORLEVEL%' NEQ '0' (
  ECHO Error: .NET Core SDK installation failed.
  EXIT /b %ERRORLEVEL%
)

REM #######################################################
REM # Delete .NET Core SDK installer.
REM #######################################################
del %dotNetInstallerPath%
