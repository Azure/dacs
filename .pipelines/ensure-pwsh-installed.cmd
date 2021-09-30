@if not defined _echo echo off
@setlocal EnableDelayedExpansion EnableExtensions
REM This script installs Powershell 7 on Windows
SET scriptPath=%~dp0
SET PwshRequiredMajor=7
SET PwshRequiredMinor=0
SET PwshRequiredBuild=5

SET PwshRequiredVersion=%PwshRequiredMajor%.%PwshRequiredMinor%.%PwshRequiredBuild%
SET pwshInstaller=PowerShell-%PwshRequiredVersion%-win-x64.msi
SET pwshInstallerPath=%scriptPath%%pwshInstaller%

REM This Url can be obtained from the https://github.com/PowerShell/PowerShell/releases/
SET downloadUrl=https://github.com/PowerShell/PowerShell/releases/download/v%PwshRequiredVersion%/%pwshInstaller%

REM #######################################################
REM # Check if required Powershell 7 is already installed.
REM #######################################################

pwsh -V >nul 2>&1
IF '%ERRORLEVEL%' NEQ '0' (
  ECHO Powershell is not installed
) ELSE (
  ECHO Powershell is installed, checking the version.
  FOR /f "delims=PowerShell " %%V in ('pwsh -V') do (
    @set ver=%%V
    ECHO Found Powershell !ver!. Required version: %PwshRequiredVersion%

    for /f "tokens=1 delims=." %%n in ("!ver!") do SET _major=%%n
    for /f "tokens=2 delims=." %%n in ("!ver!") do SET _minor=%%n
    for /f "tokens=3 delims=." %%n in ("!ver!") do SET _build=%%n

    SET /a installed_version_num=!_major!            * 1000000 + !_minor!            * 1000 + !_build!
    SET /a required_version_num =!PwshRequiredMajor! * 1000000 + !PwshRequiredMinor! * 1000 + !PwshRequiredBuild!

    IF !installed_version_num! GEQ !required_version_num! exit /b 0
  )
)

REM #######################################################
REM # Download Powershell 7 installer.
REM #######################################################

ECHO Powershell %PwshRequiredVersion% not found. Installing Powershell

ECHO Downloading Powershell installer from %downloadUrl%
powershell -noprofile -ExecutionPolicy Bypass -Command "(New-Object Net.WebClient).DownloadFile('%downloadUrl%', '%pwshInstallerPath%')"

IF '%ERRORLEVEL%' NEQ '0' (
  ECHO Error: Failed to download Powershell installer from %downloadUrl%
  EXIT /b %ERRORLEVEL%
)

ECHO Running Powershell installer from %pwshInstallerPath%
msiexec.exe /package %pwshInstallerPath% /passive ADD_EXPLORER_CONTEXT_MENU_OPENPOWERSHELL=1 REGISTER_MANIFEST=1
del %pwshInstallerPath%

endlocal
SET PATH=%PROGRAMFILES%\Powershell\7;%PATH%