@echo off

@echo Creating devcontainer for native build on vs code
for /f "delims=" %%A in ('findstr /rc:"imagelabel" build\.metadata\native-build.container.metadata') do set "%%A"
if not exist .devcontainer mkdir .devcontainer
(
    echo.{
    echo.    "name": "DACS",
    echo.    "image": "dacs.azurecr.io/native-build:%native-build-imagelabel%",
    echo.    "extensions": [
    echo.       "ms-vscode.cpptools",
    echo.       "ms-vscode.cmake-tools",
    echo.       "ms-azuretools.vscode-docker",
    echo.       "ms-dotnettools.csharp",
    echo.       "streetsidesoftware.code-spell-checker",
    echo.       "donjayamanne.githistory",
    echo.       "michelemelluso.gitignore",
    echo.       "eamodio.gitlens",
    echo.       "christian-kohler.path-intellisense",
    echo.       "jakob101.relativepath"
    echo.    ]
    echo.}
) > .devcontainer/devcontainer.json

pushd "%~dp0"
@echo Setup msbuild environment
call "build\setupenv.cmd"

@echo Enabling Powershell Execution
powershell Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope CurrentUser

SET BUILDDIR=%~dp0\src
SET BINARIESDIR=%~dp0\bin
SET PIPELINESDIR=%~dp0\.pipelines

echo ******************************************************************************
echo * Welcome to the DACS build environment. Some common commands:        *
echo *                                                                            *
echo * Build (recursively): b                Build this project only: bo          *
echo * Build in Release configuration: br    Build this project only (rel): bor   *
echo * Rebuild ('build clean'): bc           Build with logging to files: bl      *
echo * Run unit tests: ut                    Run unit tests in Release: utr       *
echo * Native unit tests : nut               Open in Visual Studio: open          *
echo * Go to the root directory: src         Go to the binaries directory: bindir *
echo * Go up the tree quickly: up1 - up7     Launch Notepad.exe: n                *
echo * Login to ACR: acrl                                                         *
echo *                                                                            *
echo * For a full list of macros consult build\macros.txt                         *
echo *                                                                            *
echo * All build macros invoke msbuild with /restore argument                     *
echo *                                                                            *
echo ******************************************************************************

rem setup common aliases
doskey /MACROFILE=build\macros.txt

cmd /k %*
